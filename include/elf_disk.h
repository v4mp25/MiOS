
#ifndef MIOS_ELF_DISK_H
#define MIOS_ELF_DISK_H

#include <stdint.h>

#define MIOS_ELF_DISK_MAX 8192U

/*
 * Lee una imagen ELF completa desde
 * el contenedor binario de prueba ATA.
 *
 * No ejecuta el archivo ni escribe
 * sobre el disco.
 */
int elf_disk_read(
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *size
);

#endif
