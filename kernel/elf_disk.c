
#include <stdint.h>

#include "elf_disk.h"
#include "ata.h"
#include "irq.h"

#define ELF_HEADER_LBA 256U
#define ELF_DATA_LBA   257U

static uint8_t sector[ATA_SECTOR_SIZE];

static const uint8_t elf_magic[8] = {
    'M', 'I', 'O', 'S', 'E', 'L', 'F', '1'
};

static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t checksum32(
    const uint8_t *data,
    uint32_t length
) {
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

int elf_disk_read(
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *size
) {
    if (buffer == 0 || size == 0)
        return -1;

    *size = 0;

    uint32_t flags = irq_save();

    if (ata_read_sector(
            ELF_HEADER_LBA,
            sector
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < 8U; i++) {
        if (sector[i] != elf_magic[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t length = get_u32(&sector[8]);
    uint32_t expected = get_u32(&sector[12]);

    if (length == 0U ||
        length > MIOS_ELF_DISK_MAX ||
        length > capacity) {

        irq_restore(flags);
        return -1;
    }

    uint32_t sectors =
        (length + ATA_SECTOR_SIZE - 1U) /
        ATA_SECTOR_SIZE;

    for (uint32_t i = 0; i < sectors; i++) {
        if (ata_read_sector(
                ELF_DATA_LBA + i,
                sector
            ) != 0) {

            irq_restore(flags);
            return -1;
        }

        uint32_t offset = i * ATA_SECTOR_SIZE;
        uint32_t remaining = length - offset;

        uint32_t count =
            remaining < ATA_SECTOR_SIZE
                ? remaining
                : ATA_SECTOR_SIZE;

        for (uint32_t j = 0; j < count; j++) {
            buffer[offset + j] = sector[j];
        }
    }

    if (checksum32(buffer, length) != expected) {
        irq_restore(flags);
        return -1;
    }

    *size = length;

    irq_restore(flags);
    return 0;
}
