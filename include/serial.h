
#ifndef MIOS_SERIAL_H
#define MIOS_SERIAL_H

#include <stdint.h>

/*
 * Puerto serie COM1.
 *
 * Diagnostico del kernel mediante QEMU.
 */
int serial_init(void);

int serial_available(void);

void serial_write_char(char c);

void serial_write(const char *text);

void serial_write_hex32(uint32_t value);

#endif
