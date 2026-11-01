
#ifndef MIOS_BINFS_VERIFY_H
#define MIOS_BINFS_VERIFY_H

#include <stdint.h>

typedef struct {
    uint32_t files_checked;
    uint32_t elf_valid;
    uint32_t errors;
} BinfsVerifyResult;

/*
 * Verifica todos los archivos activos.
 *
 * Comprueba:
 * - Lectura por nombre
 * - Checksum de contenido
 * - Estructura ELF32
 * - Nombres y LBA independientes
 *
 * No modifica el disco.
 */
int binfs_verify_all(BinfsVerifyResult *result);

#endif
