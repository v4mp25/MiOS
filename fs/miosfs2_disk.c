
#include <stdint.h>

#include "ata.h"
#include "irq.h"
#include "miosfs2.h"
#include "miosfs2_disk.h"

#define FS2_SECTOR 512U
#define FS2_BITMAP_BYTES 128U

static uint8_t fs2_super_sector[FS2_SECTOR];
static uint8_t fs2_bitmap_sector[FS2_SECTOR];
static uint8_t fs2_verify_sector[FS2_SECTOR];

/*
 * Validar disco de laboratorio.
 *
 * Rechaza cualquier distribucion
 * distinta a la utilizada en las pruebas.
 */
static int fs2_load(void) {
    if (ata_read_sector(
            MIOSFS2_SUPER_LBA,
            fs2_super_sector
        ) != 0) {
        return -1;
    }

    MiosFs2Super sb;

    uint8_t *destination = (uint8_t *)&sb;

    for (uint32_t i = 0; i < sizeof(sb); i++) {
        destination[i] = fs2_super_sector[i];
    }

    if (miosfs2_validate(&sb) != 0)
        return -1;

    /*
     * No basta con una firma valida:
     * exigimos exactamente el layout
     * reservado para el laboratorio.
     */
    if (sb.total_sectors != 32768U ||
        sb.bitmap_lba != MIOSFS2_BITMAP_LBA ||
        sb.bitmap_sectors != 1U ||
        sb.nodes_lba != MIOSFS2_NODES_LBA ||
        sb.nodes_sectors != 8U ||
        sb.data_lba != MIOSFS2_DATA_LBA ||
        sb.data_sectors != 1024U) {
        return -1;
    }

    /* El resto del sector superbloque
       debe permanecer en cero. */
    for (uint32_t i = sizeof(sb);
         i < FS2_SECTOR;
         i++) {

        if (fs2_super_sector[i] != 0U)
            return -1;
    }

    if (ata_read_sector(
            MIOSFS2_BITMAP_LBA,
            fs2_bitmap_sector
        ) != 0) {
        return -1;
    }

    /*
     * Solo los primeros 128 bytes
     * representan los 1024 bloques.
     */
    for (uint32_t i = FS2_BITMAP_BYTES;
         i < FS2_SECTOR;
         i++) {

        if (fs2_bitmap_sector[i] != 0U)
            return -1;
    }

    return 0;
}

int miosfs2_disk_check(void) {
    uint32_t flags = irq_save();

    int result = fs2_load();

    irq_restore(flags);
    return result;
}

int miosfs2_disk_block0(int *used) {
    if (used == 0)
        return -1;

    uint32_t flags = irq_save();

    if (fs2_load() != 0) {
        irq_restore(flags);
        return -1;
    }

    *used =
        (fs2_bitmap_sector[0] & 0x01U)
            ? 1 : 0;

    irq_restore(flags);
    return 0;
}

/*
 * Persistir el estado del bloque 0.
 *
 * Escritura de un sector completo,
 * seguida de una lectura de comprobacion.
 */
int miosfs2_disk_set_block0(int used) {
    if (used != 0 && used != 1)
        return -1;

    uint32_t flags = irq_save();

    if (fs2_load() != 0) {
        irq_restore(flags);
        return -1;
    }

    int current =
        (fs2_bitmap_sector[0] & 0x01U)
            ? 1 : 0;

    /* No reservar dos veces ni
       liberar dos veces. */
    if (current == used) {
        irq_restore(flags);
        return -1;
    }

    if (used) {
        fs2_bitmap_sector[0] |= 0x01U;
    } else {
        fs2_bitmap_sector[0] &=
            (uint8_t)~0x01U;
    }

    if (ata_write_sector(
            MIOSFS2_BITMAP_LBA,
            fs2_bitmap_sector
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Leer de nuevo el sector físico
     * y comparar todos sus bytes.
     */
    if (ata_read_sector(
            MIOSFS2_BITMAP_LBA,
            fs2_verify_sector
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < FS2_SECTOR; i++) {
        if (fs2_verify_sector[i] !=
            fs2_bitmap_sector[i]) {

            irq_restore(flags);
            return -1;
        }
    }

    irq_restore(flags);
    return 0;
}
