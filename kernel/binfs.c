
#include <stdint.h>

#include "binfs.h"
#include "ata.h"
#include "irq.h"

#define BINFS_CATALOG_LBA 512U
#define BINFS_DATA_LBA 513U
#define BINFS_SECTOR_SIZE 512U
#define BINFS_SECTOR_COUNT 16U

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

    /* La version inicial admite un archivo */
    if (read_u32(&catalog[8]) != 1U) {
        irq_restore(flags);
        return -1;
    }

    /* Nombre desde byte 16 */
    if (!names_equal(filename, &catalog[16])) {
        irq_restore(flags);
        return -1;
    }

    uint32_t lba = read_u32(&catalog[48]);
    uint32_t size = read_u32(&catalog[52]);
    uint32_t expected = read_u32(&catalog[56]);

    if (lba != BINFS_DATA_LBA ||
        size == 0U ||
        size > BINFS_MAX_FILE_SIZE ||
        size > capacity) {
        irq_restore(flags);
        return -1;
    }

    uint32_t sectors =
        (size + BINFS_SECTOR_SIZE - 1U) /
        BINFS_SECTOR_SIZE;

    if (sectors > BINFS_SECTOR_COUNT) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < sectors; i++) {
        if (ata_read_sector(lba + i, sector) != 0) {
            irq_restore(flags);
            return -1;
        }

        uint32_t offset = i * BINFS_SECTOR_SIZE;
        uint32_t remaining = size - offset;

        uint32_t count =
            remaining < BINFS_SECTOR_SIZE
                ? remaining
                : BINFS_SECTOR_SIZE;

        for (uint32_t j = 0; j < count; j++) {
            buffer[offset + j] = sector[j];
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
