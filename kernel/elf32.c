
#include <stdint.h>
#include <stddef.h>

#include "elf32.h"

/*
 * Copiar bytes sin depender de libc.
 */
static void elf_copy(
    void *destination,
    const void *source,
    uint32_t count
) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src =
        (const uint8_t *)source;

    for (uint32_t i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

/*
 * Validar un archivo ELF32 x86
 * ejecutable de formato little-endian.
 */
int elf32_validate(
    const void *image,
    uint32_t size,
    Elf32_Ehdr *result
) {
    if (image == NULL || result == NULL)
        return -1;

    if (size < ELF32_EHDR_SIZE)
        return -1;

    Elf32_Ehdr header;

    elf_copy(
        &header,
        image,
        sizeof(Elf32_Ehdr)
    );

    /* Identificacion ELF */
    if (header.e_ident[0] != ELF32_MAGIC0 ||
        header.e_ident[1] != ELF32_MAGIC1 ||
        header.e_ident[2] != ELF32_MAGIC2 ||
        header.e_ident[3] != ELF32_MAGIC3) {
        return -1;
    }

    /* ELF de 32 bits y little-endian */
    if (header.e_ident[4] != ELFCLASS32 ||
        header.e_ident[5] != ELFDATA2LSB ||
        header.e_ident[6] != EV_CURRENT) {
        return -1;
    }

    /* Ejecutable x86 de 32 bits */
    if (header.e_type != ET_EXEC ||
        header.e_machine != EM_386 ||
        header.e_version != EV_CURRENT) {
        return -1;
    }

    /* Tamaño correcto de cabecera */
    if (header.e_ehsize != sizeof(Elf32_Ehdr))
        return -1;

    /*
     * Por ahora exigimos al menos un
     * encabezado de programa.
     */
    if (header.e_phnum == 0)
        return -1;

    if (header.e_phentsize !=
        sizeof(Elf32_Phdr)) {
        return -1;
    }

    /*
     * Comprobar los limites sin permitir
     * desbordamiento de enteros.
     */
    if (header.e_phoff < ELF32_EHDR_SIZE ||
        header.e_phoff > size) {
        return -1;
    }

    uint32_t remaining =
        size - header.e_phoff;

    if ((uint32_t)header.e_phnum >
        remaining / sizeof(Elf32_Phdr)) {
        return -1;
    }

    /*
     * No validamos todavia los segmentos
     * PT_LOAD ni sus permisos.
     * Eso corresponde a 4.12B.
     */
    *result = header;

    return 0;
}
