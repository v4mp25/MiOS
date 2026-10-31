
#ifndef MIOS_BINFS_H
#define MIOS_BINFS_H

#include <stdint.h>

#define BINFS_MAX_FILE_SIZE 8192U

/*
 * Leer un archivo binario por nombre.
 * Devuelve el tamano real en bytes_read.
 *
 * Es una interfaz experimental de lectura.
 */
int binfs_read(
    const char *filename,
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *bytes_read
);

#endif
