#include <stdint.h>
#include "ata.h"

/*
 * Controlador ATA PIO para el disco IDE primario maestro de QEMU.
 * Soporta LBA28 y transfiere un sector de 512 bytes por llamada.
 * Devuelve 0 si tiene exito y -1 si detecta un error/timeout.
 */

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_ERR 0x01
#define ATA_DRQ 0x08
#define ATA_DF  0x20
#define ATA_BSY 0x80

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

#define ATA_TIMEOUT 1000000U
#define ATA_LBA28_LIMIT 0x10000000U

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ata_delay_400ns(void) {
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
}

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0x00 || status == 0xFF)
            return -1;
        if ((status & ATA_BSY) == 0)
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF)
            return -1;
        if (status & ATA_BSY)
            continue;
        if (status & (ATA_ERR | ATA_DF))
            return -1;
        if (status & ATA_DRQ)
            return 0;
    }
    return -1;
}

static int ata_select_lba28(uint32_t lba) {
    if (lba >= ATA_LBA28_LIMIT)
        return -1;

    /* Primero seleccionamos el disco maestro en modo LBA. */
    outb(ATA_DRIVE,
         (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));

    ata_delay_400ns();

    /* Después esperamos a que el dispositivo esté disponible. */
    if (ata_wait_not_busy() != 0)
        return -1;

    /* Configuramos una transferencia de un sector. */
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    return 0;
}

/* Lee un sector LBA de 512 bytes en buffer. */
int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (buffer == 0 || ata_select_lba28(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_READ);
    ata_delay_400ns();

    if (ata_wait_drq() != 0)
        return -1;

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        uint16_t word = inw(ATA_DATA);
        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return ata_wait_not_busy();
}

/* Escribe un sector LBA de 512 bytes y solicita vaciar la cache del disco. */
int ata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (buffer == 0 || ata_select_lba28(lba) != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_WRITE);
    ata_delay_400ns();

    if (ata_wait_drq() != 0)
        return -1;

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        uint16_t word = (uint16_t)buffer[i * 2]
                      | ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(ATA_DATA, word);
    }

    if (ata_wait_not_busy() != 0)
        return -1;

    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_delay_400ns();
    return ata_wait_not_busy();
}
