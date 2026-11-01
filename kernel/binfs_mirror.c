
#include <stdint.h>

#include "binfs_mirror.h"
#include "ata.h"
#include "irq.h"
#include <stddef.h>
/*
 * Etapa 4.14C.3D.1
 *
 * Verificacion de dos copias del catalogo.
 * No realiza escrituras ni recuperaciones.
 */

static uint8_t catalog_primary[ATA_SECTOR_SIZE];
static uint8_t catalog_mirror[ATA_SECTOR_SIZE];

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
