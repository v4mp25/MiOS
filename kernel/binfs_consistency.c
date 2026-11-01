
#include <stdint.h>
#include <stddef.h>

#include "binfs_consistency.h"
#include "binfs_mirror.h"
#include "ata.h"
#include "irq.h"

#define CATALOG_SIZE 512U
#define ENTRY_START 16U
#define ENTRY_SIZE 64U
#define MAX_SLOTS 4U

#define FILE_MAX 8192U
#define SECTORS_PER_FILE 16U

#define DATA_A_LBA 513U
#define DATA_B_LBA 1024U

#define GEN_OFFSET 480U
#define HASH_OFFSET 484U
#define VERSION_OFFSET 488U

static uint8_t catalog_a[CATALOG_SIZE];
static uint8_t catalog_b[CATALOG_SIZE];
static uint8_t data_sector[ATA_SECTOR_SIZE];

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t fnv_update(
    uint32_t hash,
    uint8_t value
) {
    hash ^= value;
    hash *= 16777619U;
    return hash;
}

static uint32_t catalog_checksum(
    const uint8_t *data
) {
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < CATALOG_SIZE; i++) {
        uint8_t value = data[i];

        if (i >= HASH_OFFSET &&
            i < HASH_OFFSET + 4U) {
            value = 0;
        }

        hash = fnv_update(hash, value);
    }

    return hash;
}

static int valid_catalog(
    const uint8_t *data
) {
    static const uint8_t magic[8] = {
        'M', 'I', 'O', 'S', 'B', 'I', 'N', '1'
    };

    for (uint32_t i = 0; i < 8U; i++) {
        if (data[i] != magic[i])
            return 0;
    }

    uint32_t slots = read32(&data[8]);
    uint32_t generation = read32(&data[GEN_OFFSET]);
    uint32_t version = read32(&data[VERSION_OFFSET]);

    if (slots == 0U || slots > MAX_SLOTS ||
        generation == 0U || version != 2U) {
        return 0;
    }

    return read32(&data[HASH_OFFSET]) ==
           catalog_checksum(data);
}

/*
 * Comprobar el contenido de un archivo
 * leyendo todos sus sectores.
 */
static int verify_file(
    const uint8_t *entry,
    uint32_t slot
) {
    if (entry[0] == 0U)
        return -1;

    /* El nombre debe terminar en NUL */
    int terminated = 0;

    for (uint32_t i = 0; i < 32U; i++) {
        if (entry[i] == 0U) {
            terminated = 1;
            break;
        }
    }

    if (!terminated)
        return -1;

    uint32_t lba = read32(entry + 32U);
    uint32_t size = read32(entry + 36U);
    uint32_t expected = read32(entry + 40U);

    uint32_t region_a =
        DATA_A_LBA + slot * SECTORS_PER_FILE;

    uint32_t region_b =
        DATA_B_LBA + slot * SECTORS_PER_FILE;

    if ((lba != region_a && lba != region_b) ||
        size == 0U || size > FILE_MAX) {
        return -1;
    }

    uint32_t hash = 2166136261U;

    uint32_t sectors =
        (size + ATA_SECTOR_SIZE - 1U) /
        ATA_SECTOR_SIZE;

    for (uint32_t s = 0; s < sectors; s++) {
        if (ata_read_sector(
                lba + s,
                data_sector
            ) != 0) {
            return -1;
        }

        uint32_t offset = s * ATA_SECTOR_SIZE;
        uint32_t remaining = size - offset;

        uint32_t length =
            remaining < ATA_SECTOR_SIZE
                ? remaining
                : ATA_SECTOR_SIZE;

        for (uint32_t j = 0; j < length; j++) {
            hash = fnv_update(
                hash,
                data_sector[j]
            );
        }
    }

    return hash == expected ? 0 : -1;
}

static void inspect_catalog(
    const uint8_t *catalog,
    BinfsCopyReport *report
) {
    report->catalog_valid = 0;
    report->generation = 0;
    report->files_checked = 0;
    report->files_ok = 0;
    report->files_damaged = 0;
    report->data_consistent = 0;

    if (!valid_catalog(catalog))
        return;

    report->catalog_valid = 1;
    report->generation =
        read32(&catalog[GEN_OFFSET]);

    uint32_t slots = read32(&catalog[8]);

    for (uint32_t i = 0; i < slots; i++) {
        uint32_t offset =
            ENTRY_START + i * ENTRY_SIZE;

        const uint8_t *entry = catalog + offset;

        /* Posicion eliminada */
        if (entry[0] == 0U) {
            /*
             * Tombstone completo:
             * los 64 bytes deben ser cero.
             */
            int empty = 1;

            for (uint32_t j = 0; j < ENTRY_SIZE; j++) {
                if (entry[j] != 0U) {
                    empty = 0;
                    break;
                }
            }

            if (!empty)
                report->files_damaged++;

            continue;
        }

        report->files_checked++;

        if (verify_file(entry, i) == 0)
            report->files_ok++;
        else
            report->files_damaged++;
    }

    report->data_consistent =
        (report->files_damaged == 0U &&
         report->files_ok == report->files_checked);
}

int binfs_consistency_check(
    BinfsConsistencyReport *result
) {
    if (result == NULL)
        return -1;

    uint32_t flags = irq_save();

    int read_a = ata_read_sector(
        BINFS_PRIMARY_CATALOG_LBA,
        catalog_a
    );

    int read_b = ata_read_sector(
        BINFS_MIRROR_CATALOG_LBA,
        catalog_b
    );

    if (read_a == 0) {
        inspect_catalog(catalog_a, &result->a);
    } else {
        result->a = (BinfsCopyReport){0};
    }

    if (read_b == 0) {
        inspect_catalog(catalog_b, &result->b);
    } else {
        result->b = (BinfsCopyReport){0};
    }

    result->selected_lba = 0;
    result->selected_generation = 0;
    result->selected_data_ok = 0;

    /*
     * Utilizar la seleccion que ya tiene
     * MiOS para el catalogo vigente.
     */
    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) == 0) {
        result->selected_lba = status.selected_lba;
        result->selected_generation =
            status.selected_generation;

        if (status.selected_lba ==
            BINFS_PRIMARY_CATALOG_LBA) {

            result->selected_data_ok =
                result->a.data_consistent;

        } else if (status.selected_lba ==
                   BINFS_MIRROR_CATALOG_LBA) {

            result->selected_data_ok =
                result->b.data_consistent;
        }
    }

    irq_restore(flags);

    return (read_a == 0 || read_b == 0) ? 0 : -1;
}

/*
 * Determinar si la version activa
 * y la alternativa tienen datos integros.
 *
 * Solo lectura.
 */
int binfs_recovery_check(
    BinfsRecoveryReport *result
) {
    if (result == NULL)
        return -1;

    BinfsConsistencyReport report;

    if (binfs_consistency_check(&report) != 0)
        return -1;

    result->active_lba = report.selected_lba;
    result->active_generation =
        report.selected_generation;

    result->active_data_ok = 0;
    result->fallback_data_ok = 0;
    result->recoverable_catalogs = 0;
    result->full_fallback_available = 0;

    /*
     * Un catalogo es recuperable solo
     * cuando sus metadatos y todos los
     * datos referenciados son integros.
     */
    uint32_t valid_a =
        report.a.catalog_valid &&
        report.a.data_consistent;

    uint32_t valid_b =
        report.b.catalog_valid &&
        report.b.data_consistent;

    result->recoverable_catalogs =
        valid_a + valid_b;

    if (report.selected_lba ==
        BINFS_PRIMARY_CATALOG_LBA) {

        result->active_data_ok = valid_a;
        result->fallback_data_ok = valid_b;

    } else if (report.selected_lba ==
               BINFS_MIRROR_CATALOG_LBA) {

        result->active_data_ok = valid_b;
        result->fallback_data_ok = valid_a;

    } else {
        return -1;
    }

    /*
     * Una segunda version completa
     * existe cuando el otro catalogo
     * tambien es coherente con sus datos.
     */
    result->full_fallback_available =
        result->fallback_data_ok ? 1U : 0U;

    return 0;
}

/*
 * Seleccionar una version recuperable.
 *
 * A diferencia del lector anterior,
 * tambien exige que todos los archivos
 * referenciados conserven su checksum.
 *
 * No modifica el disco.
 */
int binfs_catalog_read_recoverable(
    uint8_t *destination
) {
    if (destination == NULL)
        return -1;

    uint32_t flags = irq_save();

    BinfsConsistencyReport report;

    if (binfs_consistency_check(&report) != 0) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Rechazar discrepancias ambiguas
     * entre catalogos con la misma
     * generacion.
     */
    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0) {
        irq_restore(flags);
        return -1;
    }

    int valid_a =
        report.a.catalog_valid &&
        report.a.data_consistent;

    int valid_b =
        report.b.catalog_valid &&
        report.b.data_consistent;

    if (!valid_a && !valid_b) {
        irq_restore(flags);
        return -1;
    }

    uint32_t selected_lba;

    if (valid_a && valid_b) {
        selected_lba =
            report.a.generation >=
            report.b.generation
                ? BINFS_PRIMARY_CATALOG_LBA
                : BINFS_MIRROR_CATALOG_LBA;

    } else if (valid_a) {
        selected_lba =
            BINFS_PRIMARY_CATALOG_LBA;

    } else {
        selected_lba =
            BINFS_MIRROR_CATALOG_LBA;
    }

    /*
     * Volver a leer la copia seleccionada.
     * Conservamos IRQ deshabilitadas para
     * impedir modificaciones concurrentes
     * desde las operaciones del kernel.
     */
    if (ata_read_sector(
            selected_lba,
            destination
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Comprobar que no cambio con respecto
     * al catalogo que se diagnostico.
     */
    const uint8_t *verified =
        selected_lba == BINFS_PRIMARY_CATALOG_LBA
            ? catalog_a
            : catalog_b;

    for (uint32_t i = 0; i < CATALOG_SIZE; i++) {
        if (destination[i] != verified[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t slots = read32(&destination[8]);

    irq_restore(flags);

    if (slots == 0U || slots > MAX_SLOTS)
        return -1;

    return (int)slots;
}

/*
 * No permitir escrituras si alguna copia
 * carece de metadatos o datos integros.
 *
 * Esto es una politica conservadora:
 * antes de reparar o actualizar,
 * detener operaciones de modificacion.
 */
int binfs_recovery_write_allowed(void) {
    uint32_t flags = irq_save();

    BinfsConsistencyReport report;

    if (binfs_consistency_check(&report) != 0) {
        irq_restore(flags);
        return 0;
    }

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0) {
        irq_restore(flags);
        return 0;
    }

    int allowed =
        report.a.catalog_valid &&
        report.b.catalog_valid &&
        report.a.data_consistent &&
        report.b.data_consistent;

    irq_restore(flags);
    return allowed ? 1 : 0;
}
