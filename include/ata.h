#ifndef MIOS_ATA_H
#define MIOS_ATA_H
#include <stdint.h>
#define ATA_SECTOR_SIZE 512
int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);
#endif
