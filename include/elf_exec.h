
#ifndef MIOS_ELF_EXEC_H
#define MIOS_ELF_EXEC_H

#include <stdint.h>

/* Ejecutar una imagen ELF32 desde memoria */
int elf_exec_image(
    const void *image,
    uint32_t size
);

/* Buscar y ejecutar un ELF32 desde BINFS */
int elf_exec_file(const char *filename);

#endif
