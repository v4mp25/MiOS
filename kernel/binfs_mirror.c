
#include <stdint.h>

#include "binfs_mirror.h"
#include "ata.h"
#include "irq.h"

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
