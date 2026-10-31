
#ifndef MIOS_ELF_EXEC_H
#define MIOS_ELF_EXEC_H

#include <stdint.h>

/*
 * Ejecuta una imagen ELF32 completa
 * almacenada en un buffer del kernel.
 *
 * Esta interfaz no lee el disco.
 * El sistema de archivos proporcionara
 * la imagen y su tamano real.
 */
int elf_exec_image(
    const void *image,
    uint32_t size
);

/*
 * Futura interfaz para ejecutar archivos
 * almacenados en el disco ATA.
 */
int elf_exec_file(const char *filename);

#endif
