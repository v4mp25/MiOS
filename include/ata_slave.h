#ifndef MIOS_ATA_SLAVE_H
#define MIOS_ATA_SLAVE_H
#include <stdint.h>
/* QEMU IDE primary-channel SLAVE, mapped by -drive if=ide,index=1.
 * Separate from MiOS's existing primary MASTER and its BINFS v2.
 */
int ata_slave_read_sector(uint32_t lba, uint8_t *buffer);
int ata_slave_write_sector(uint32_t lba, const uint8_t *buffer);
#endif
