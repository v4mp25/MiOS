
#include <stdint.h>

#include "binfs.h"
#include "ata.h"
#include "irq.h"

#define BINFS_CATALOG_LBA 512U
#define BINFS_DATA_LBA 513U

#define BINFS_SECTOR_SIZE 512U
#define BINFS_SECTORS_PER_FILE 16U

#define BINFS_MAX_FILES 3U
#define BINFS_ENTRY_SIZE 64U
#define BINFS_FIRST_ENTRY 16U

static uint8_t catalog[BINFS_SECTOR_SIZE];
static uint8_t sector[BINFS_SECTOR_SIZE];

static const uint8_t magic[8] = {
    'M', 'I', 'O', 'S', 'B', 'I', 'N', '1'
};

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t checksum32(
    const uint8_t *data,
    uint32_t size
) {
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

/*
 * Comparar un nombre del comando run
 * con un nombre almacenado en 32 bytes.
 *
 * El nombre debe terminar en cero.
 */
static int names_equal(
    const char *requested,
    const uint8_t *stored
) {
    if (requested == 0)
        return 0;

    for (uint32_t i = 0; i < 32U; i++) {
        char a = requested[i];
        char b = (char)stored[i];

        if (a != b)
            return 0;

        if (a == '\0')
            return 1;
    }

    return 0;
}

int binfs_read(
    const char *filename,
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *bytes_read
) {
    if (filename == 0 ||
        buffer == 0 ||
        bytes_read == 0) {
        return -1;
    }

    *bytes_read = 0;

    uint32_t flags = irq_save();

    if (ata_read_sector(
            BINFS_CATALOG_LBA,
            catalog
        ) != 0) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < 8U; i++) {
        if (catalog[i] != magic[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t count = read_u32(&catalog[8]);

    if (count == 0U || count > BINFS_MAX_FILES) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t index = 0;
         index < count;
         index++) {

        uint32_t offset =
            BINFS_FIRST_ENTRY +
            index * BINFS_ENTRY_SIZE;

        const uint8_t *entry =
            &catalog[offset];

        if (!names_equal(filename, entry))
            continue;

        uint32_t lba = read_u32(entry + 32U);
        uint32_t size = read_u32(entry + 36U);
        uint32_t expected = read_u32(entry + 40U);

        /*
         * En esta version cada posicion
         * tiene una region fija exclusiva.
         *
         * No se permite que el catalogo
         * redirija una lectura fuera de ella.
         */
        uint32_t permitted_lba =
            BINFS_DATA_LBA +
            index * BINFS_SECTORS_PER_FILE;

        if (lba != permitted_lba ||
            size == 0U ||
            size > BINFS_MAX_FILE_SIZE ||
            size > capacity) {

            irq_restore(flags);
            return -1;
        }

        uint32_t sectors =
            (size + BINFS_SECTOR_SIZE - 1U) /
            BINFS_SECTOR_SIZE;

        if (sectors > BINFS_SECTORS_PER_FILE) {
            irq_restore(flags);
            return -1;
        }

        for (uint32_t s = 0; s < sectors; s++) {
            if (ata_read_sector(
                    lba + s,
                    sector
                ) != 0) {

                irq_restore(flags);
                return -1;
            }

            uint32_t position =
                s * BINFS_SECTOR_SIZE;

            uint32_t remaining =
                size - position;

            uint32_t count_bytes =
                remaining < BINFS_SECTOR_SIZE
                    ? remaining
                    : BINFS_SECTOR_SIZE;

            for (uint32_t j = 0;
                 j < count_bytes;
                 j++) {

                buffer[position + j] = sector[j];
            }
        }

        if (checksum32(buffer, size) != expected) {
            irq_restore(flags);
            return -1;
        }

        *bytes_read = size;

        irq_restore(flags);
        return 0;
    }

    irq_restore(flags);
    return -1;
}
