#include <stdint.h>
#include "ata_slave.h"

/* ATA PIO: dedicated PRIMARY SLAVE (0xF0), NOT the existing master (0xE0).
 * Caller must serialize calls to the shared primary ATA channel.
 * This laboratory driver supports LBA28 and one sector per command.
 */
#define ATA_DATA       0x1F0U
#define ATA_SECCOUNT   0x1F2U
#define ATA_LBA_LOW    0x1F3U
#define ATA_LBA_MID    0x1F4U
#define ATA_LBA_HIGH   0x1F5U
#define ATA_DRIVE      0x1F6U
#define ATA_STATUS     0x1F7U
#define ATA_COMMAND    0x1F7U
#define ATA_ALT_STATUS 0x3F6U
#define ATA_ERR        0x01U
#define ATA_DRQ        0x08U
#define ATA_DF         0x20U
#define ATA_BSY        0x80U
#define ATA_TIMEOUT    1000000U

static inline uint8_t slave_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t slave_inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void slave_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void slave_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void slave_400ns(void)
{
    for (uint32_t n = 0U; n < 4U; n++) (void)slave_inb(ATA_ALT_STATUS);
}

static int slave_not_busy(void)
{
    for (uint32_t n = 0U; n < ATA_TIMEOUT; n++) {
        uint8_t st = slave_inb(ATA_STATUS);
        if (st == 0U || st == 0xFFU || (st & (ATA_ERR | ATA_DF)))
            return -1;
        if ((st & ATA_BSY) == 0U) return 0;
    }
    return -1;
}

static int slave_wait_drq(void)
{
    for (uint32_t n = 0U; n < ATA_TIMEOUT; n++) {
        uint8_t st = slave_inb(ATA_STATUS);
        if (st == 0U || st == 0xFFU) return -1;
        if (st & ATA_BSY) continue;
        if (st & (ATA_ERR | ATA_DF)) return -1;
        if (st & ATA_DRQ) return 0;
    }
    return -1;
}

static int slave_select(uint32_t lba)
{
    if (lba >= 0x10000000U) return -1;
    if (slave_not_busy() != 0) return -1;
    slave_outb(ATA_DRIVE, (uint8_t)(0xF0U | ((lba >> 24U) & 15U)));
    slave_400ns();
    if (slave_not_busy() != 0) return -1;
    slave_outb(ATA_SECCOUNT, 1U);
    slave_outb(ATA_LBA_LOW, (uint8_t)lba);
    slave_outb(ATA_LBA_MID, (uint8_t)(lba >> 8U));
    slave_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16U));
    return 0;
}

int ata_slave_read_sector(uint32_t lba, uint8_t *buffer)
{
    if (!buffer || slave_select(lba) != 0) return -1;
    slave_outb(ATA_COMMAND, 0x20U);
    slave_400ns();
    if (slave_wait_drq() != 0) return -1;
    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t word = slave_inw(ATA_DATA);
        buffer[i * 2U] = (uint8_t)word;
        buffer[i * 2U + 1U] = (uint8_t)(word >> 8U);
    }
    return slave_not_busy();
}

int ata_slave_write_sector(uint32_t lba, const uint8_t *buffer)
{
    if (!buffer || slave_select(lba) != 0) return -1;
    slave_outb(ATA_COMMAND, 0x30U);
    slave_400ns();
    if (slave_wait_drq() != 0) return -1;
    for (uint32_t i = 0U; i < 256U; i++) {
        uint16_t word = (uint16_t)buffer[i * 2U] |
                        ((uint16_t)buffer[i * 2U + 1U] << 8U);
        slave_outw(ATA_DATA, word);
    }
    if (slave_not_busy() != 0) return -1;
    slave_outb(ATA_COMMAND, 0xE7U); /* FLUSH CACHE */
    slave_400ns();
    return slave_not_busy();
}
