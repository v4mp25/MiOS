
#include <stdint.h>

#include "binfs_mirror.h"
#include "ata.h"
#include "irq.h"
#include <stddef.h>
#include "binfs_consistency.h"
/*
 * Etapa 4.14C.3D.1
 *
 * Verificacion de dos copias del catalogo.
 * No realiza escrituras ni recuperaciones.
 */

static uint8_t catalog_primary[ATA_SECTOR_SIZE];
static uint8_t catalog_mirror[ATA_SECTOR_SIZE];
/* Buffers exclusivos para reparar catalogos */
static uint8_t repair_source[ATA_SECTOR_SIZE];
static uint8_t repair_verify[ATA_SECTOR_SIZE];
/* Buffers para publicacion controlada */
static uint8_t publish_current[ATA_SECTOR_SIZE];
static uint8_t publish_candidate[ATA_SECTOR_SIZE];
static uint8_t publish_verify[ATA_SECTOR_SIZE];


static const uint8_t expected_magic[8] = {
    'M', 'I', 'O', 'S', 'B', 'I', 'N', '1'
};

static int valid_catalog_header(
    const uint8_t *data
) {
    for (uint32_t i = 0; i < 8U; i++) {
        if (data[i] != expected_magic[i])
            return -1;
    }

    uint32_t slots =
        (uint32_t)data[8] |
        ((uint32_t)data[9] << 8) |
        ((uint32_t)data[10] << 16) |
        ((uint32_t)data[11] << 24);

    if (slots == 0U || slots > 4U)
        return -1;

    return 0;
}

int binfs_mirror_verify(void) {
    uint32_t flags = irq_save();

    /* Leer catalogo principal */
    if (ata_read_sector(
            BINFS_PRIMARY_CATALOG_LBA,
            catalog_primary
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Leer catalogo secundario */
    if (ata_read_sector(
            BINFS_MIRROR_CATALOG_LBA,
            catalog_mirror
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Comprobar ambas cabeceras */
    if (valid_catalog_header(catalog_primary) != 0 ||
        valid_catalog_header(catalog_mirror) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Comparar los 512 bytes */
    for (uint32_t i = 0;
         i < ATA_SECTOR_SIZE;
         i++) {

        if (catalog_primary[i] != catalog_mirror[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    irq_restore(flags);
    return 0;
}

#define GEN_OFFSET 480U
#define HASH_OFFSET 484U
#define VERSION_OFFSET 488U
#define BINFS_FORMAT_VERSION 2U

static uint32_t binfs_read_u32(
    const uint8_t *p
) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/*
 * FNV-1a sobre 512 bytes,
 * suponiendo cero en el campo checksum.
 */
static uint32_t binfs_catalog_hash(
    const uint8_t *data
) {
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++) {
        uint8_t value = data[i];

        if (i >= HASH_OFFSET &&
            i < HASH_OFFSET + 4U) {
            value = 0;
        }

        hash ^= value;
        hash *= 16777619U;
    }

    return hash;
}

/* Validar una copia del nuevo catalogo */
static int binfs_catalog_valid_v2(
    const uint8_t *data
) {
    if (valid_catalog_header(data) != 0)
        return 0;

    if (binfs_read_u32(
            &data[VERSION_OFFSET]
        ) != BINFS_FORMAT_VERSION)
        return 0;

    uint32_t generation =
        binfs_read_u32(&data[GEN_OFFSET]);

    if (generation == 0U)
        return 0;

    uint32_t stored_hash =
        binfs_read_u32(&data[HASH_OFFSET]);

    uint32_t calculated_hash =
        binfs_catalog_hash(data);

    return stored_hash == calculated_hash;
}

int binfs_generation_check(
    BinfsGenerationStatus *result
) {
    if (result == NULL)
        return -1;

    uint32_t flags = irq_save();

    result->primary_generation = 0;
    result->mirror_generation = 0;
    result->selected_generation = 0;

    result->primary_valid = 0;
    result->mirror_valid = 0;
    result->selected_lba = 0;

    int read_primary = ata_read_sector(
        BINFS_PRIMARY_CATALOG_LBA,
        catalog_primary
    );

    int read_mirror = ata_read_sector(
        BINFS_MIRROR_CATALOG_LBA,
        catalog_mirror
    );

    if (read_primary == 0 &&
        binfs_catalog_valid_v2(catalog_primary)) {

        result->primary_valid = 1;

        result->primary_generation =
            binfs_read_u32(
                &catalog_primary[GEN_OFFSET]
            );
    }

    if (read_mirror == 0 &&
        binfs_catalog_valid_v2(catalog_mirror)) {

        result->mirror_valid = 1;

        result->mirror_generation =
            binfs_read_u32(
                &catalog_mirror[GEN_OFFSET]
            );
    }

    if (!result->primary_valid &&
        !result->mirror_valid) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Si ambas generaciones coinciden,
     * exigir que los catalogos sean
     * identicos. No adivinar cual es bueno.
     */
    if (result->primary_valid &&
        result->mirror_valid &&
        result->primary_generation ==
            result->mirror_generation) {

        for (uint32_t i = 0;
             i < ATA_SECTOR_SIZE;
             i++) {

            if (catalog_primary[i] != catalog_mirror[i]) {
                irq_restore(flags);
                return -1;
            }
        }
    }

    /*
     * Primera version: comparacion normal.
     * Antes del desbordamiento de UINT32,
     * introduciremos una politica de
     * generaciones con wraparound.
     */
    if (result->primary_valid &&
        (!result->mirror_valid ||
         result->primary_generation >=
             result->mirror_generation)) {

        result->selected_generation =
            result->primary_generation;

        result->selected_lba =
            BINFS_PRIMARY_CATALOG_LBA;

    } else {
        result->selected_generation =
            result->mirror_generation;

        result->selected_lba =
            BINFS_MIRROR_CATALOG_LBA;
    }

    irq_restore(flags);
    return 0;
}

/*
 * Seleccionar y copiar el catalogo
 * BINFS v2 valido mas reciente.
 *
 * No escribe sectores ATA.
 */
int binfs_catalog_read_latest(
    uint8_t *destination
) {
    if (destination == 0)
        return -1;

    uint32_t flags = irq_save();

    int read_a = ata_read_sector(
        BINFS_PRIMARY_CATALOG_LBA,
        catalog_primary
    );

    int read_b = ata_read_sector(
        BINFS_MIRROR_CATALOG_LBA,
        catalog_mirror
    );

    int valid_a =
        read_a == 0 &&
        binfs_catalog_valid_v2(catalog_primary);

    int valid_b =
        read_b == 0 &&
        binfs_catalog_valid_v2(catalog_mirror);

    if (!valid_a && !valid_b) {
        irq_restore(flags);
        return -1;
    }

    const uint8_t *selected = 0;

    if (valid_a && valid_b) {
        uint32_t generation_a =
            binfs_read_u32(
                &catalog_primary[GEN_OFFSET]
            );

        uint32_t generation_b =
            binfs_read_u32(
                &catalog_mirror[GEN_OFFSET]
            );

        if (generation_a == generation_b) {
            /*
             * Dos generaciones iguales
             * deben contener exactamente
             * los mismos metadatos.
             */
            for (uint32_t i = 0;
                 i < ATA_SECTOR_SIZE;
                 i++) {

                if (catalog_primary[i] !=
                    catalog_mirror[i]) {

                    irq_restore(flags);
                    return -1;
                }
            }

            selected = catalog_primary;

        } else if (generation_a > generation_b) {
            selected = catalog_primary;

        } else {
            selected = catalog_mirror;
        }

    } else if (valid_a) {
        selected = catalog_primary;

    } else {
        selected = catalog_mirror;
    }

    /* Copiar el catalogo seleccionado */
    for (uint32_t i = 0;
         i < ATA_SECTOR_SIZE;
         i++) {

        destination[i] = selected[i];
    }

    uint32_t slots =
        binfs_read_u32(&destination[8]);

    irq_restore(flags);

    if (slots == 0U || slots > 4U)
        return -1;

    return (int)slots;
}

int binfs_catalog_current(
    uint32_t *generation,
    uint32_t *active_lba
) {
    if (generation == 0 || active_lba == 0)
        return -1;

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0)
        return -1;

    *generation = status.selected_generation;
    *active_lba = status.selected_lba;

    return 0;
}

/*
 * Publicacion A/B del catalogo BINFS v2.
 *
 * No altera la copia actualmente activa.
 * Escribe la version siguiente en la
 * copia inactiva y verifica la escritura.
 */
int binfs_catalog_publish(
    const uint8_t *updated,
    uint32_t expected_generation
) {
    
    /*
     * No actualizar metadatos cuando
     * existe una copia sin datos integros.
     */
    if (!binfs_recovery_write_allowed()) {
        return -1;
    }

    if (updated == 0)
        return -1;

    uint32_t flags = irq_save();

    /* Consultar la copia activa */
    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0) {
        irq_restore(flags);
        return -1;
    }

    if (status.selected_generation !=
        expected_generation) {
        irq_restore(flags);
        return -1;
    }

    /* No permitir desbordamiento de generacion */
    if (expected_generation == 0U ||
        expected_generation == UINT32_MAX) {
        irq_restore(flags);
        return -1;
    }

    /* Recuperar el catalogo vigente */
    int slots = binfs_catalog_read_latest(
        publish_current
    );

    if (slots < 1 || slots > 4) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Verificar que los metadatos base
     * del candidato procedan de una
     * version BINFS v2 conocida.
     */
    if (valid_catalog_header(updated) != 0) {
        irq_restore(flags);
        return -1;
    }

    if (binfs_read_u32(
            &updated[VERSION_OFFSET]
        ) != BINFS_FORMAT_VERSION) {
        irq_restore(flags);
        return -1;
    }

    if (binfs_read_u32(
            &updated[GEN_OFFSET]
        ) != expected_generation) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Copiar el candidato para poder
     * actualizar generacion y checksum
     * sin modificar el buffer del llamador.
     */
    for (uint32_t i = 0;
         i < ATA_SECTOR_SIZE;
         i++) {
        publish_candidate[i] = updated[i];
    }

    uint32_t next_generation =
        expected_generation + 1U;

    /* Escribir generacion nueva */
    for (uint32_t i = 0; i < 4U; i++) {
        publish_candidate[GEN_OFFSET + i] =
            (uint8_t)(
                next_generation >> (8U * i)
            );
    }

    /* Poner a cero el campo checksum */
    for (uint32_t i = 0; i < 4U; i++) {
        publish_candidate[HASH_OFFSET + i] = 0;
    }

    /* Calcular checksum del catalogo entero */
    uint32_t hash =
        binfs_catalog_hash(publish_candidate);

    for (uint32_t i = 0; i < 4U; i++) {
        publish_candidate[HASH_OFFSET + i] =
            (uint8_t)(hash >> (8U * i));
    }

    /* Validar el candidato final */
    if (!binfs_catalog_valid_v2(
            publish_candidate
        )) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Elegir el sector INACTIVO.
     *
     * Si A esta activo, escribir B.
     * Si B esta activo, escribir A.
     */
    uint32_t target_lba;

    if (status.selected_lba ==
        BINFS_PRIMARY_CATALOG_LBA) {

        target_lba =
            BINFS_MIRROR_CATALOG_LBA;

    } else if (status.selected_lba ==
               BINFS_MIRROR_CATALOG_LBA) {

        target_lba =
            BINFS_PRIMARY_CATALOG_LBA;

    } else {
        irq_restore(flags);
        return -1;
    }

    /* Publicar en la copia inactiva */
    if (ata_write_sector(
            target_lba,
            publish_candidate
        ) != 0) {
        irq_restore(flags);
        return -1;
    }

    /* Leer nuevamente el sector escrito */
    if (ata_read_sector(
            target_lba,
            publish_verify
        ) != 0) {
        irq_restore(flags);
        return -1;
    }

    /* Comparar los 512 bytes */
    for (uint32_t i = 0;
         i < ATA_SECTOR_SIZE;
         i++) {

        if (publish_verify[i] !=
            publish_candidate[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    /*
     * Confirmar que el lector seleccione
     * la generacion recien publicada.
     */
    BinfsGenerationStatus after;

    if (binfs_generation_check(&after) != 0 ||
        after.selected_lba != target_lba ||
        after.selected_generation !=
            next_generation) {

        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}

/*
 * MiOS - Reparacion de catalogo BINFS v2.
 *
 * Copia los 512 bytes del catalogo valido
 * sobre el catalogo invalido.
 *
 * No modifica sectores de archivos.
 */
int binfs_catalog_repair(void) {
    uint32_t flags = irq_save();

    BinfsGenerationStatus status;

    /* Identificar las copias validas */
    if (binfs_generation_check(&status) != 0) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Exigir exactamente una copia valida.
     * Si ambas son validas, no reparar.
     */
    if (status.primary_valid ==
        status.mirror_valid) {

        irq_restore(flags);
        return -1;
    }

    uint32_t source_lba = 0;
    uint32_t target_lba = 0;

    if (status.primary_valid) {
        source_lba =
            BINFS_PRIMARY_CATALOG_LBA;

        target_lba =
            BINFS_MIRROR_CATALOG_LBA;
    } else {
        source_lba =
            BINFS_MIRROR_CATALOG_LBA;

        target_lba =
            BINFS_PRIMARY_CATALOG_LBA;
    }

    /*
     * Leer nuevamente el catalogo valido.
     */
    if (ata_read_sector(
            source_lba,
            repair_source
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Comprobar su checksum y formato */
    if (!binfs_catalog_valid_v2(
            repair_source
        )) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Verificar que no haya cambiado
     * la generacion durante la operacion.
     */
    uint32_t generation =
        binfs_read_u32(
            &repair_source[GEN_OFFSET]
        );

    if (generation !=
        status.selected_generation) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Escribir solo la copia invalida.
     * La fuente valida queda intacta.
     */
    if (ata_write_sector(
            target_lba,
            repair_source
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Verificar el sector reparado */
    if (ata_read_sector(
            target_lba,
            repair_verify
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0;
         i < ATA_SECTOR_SIZE;
         i++) {

        if (repair_verify[i] !=
            repair_source[i]) {

            irq_restore(flags);
            return -1;
        }
    }

    /* Confirmar formato y checksum */
    if (!binfs_catalog_valid_v2(
            repair_verify
        )) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Ambas copias deben ser validas
     * y contener la misma generacion.
     */
    BinfsGenerationStatus after;

    if (binfs_generation_check(&after) != 0 ||
        !after.primary_valid ||
        !after.mirror_valid ||
        after.primary_generation != generation ||
        after.mirror_generation != generation) {

        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}
