
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

/*
 * Validacion de segmentos ELF32 PT_LOAD.
 *
 * Version inicial para una unica pagina
 * de codigo/datos USER de MiOS.
 */
int elf32_validate_segments(
    const void *image,
    uint32_t size,
    Elf32_LoadInfo *result
) {
    if (image == NULL || result == NULL)
        return -1;

    Elf32_Ehdr header;

    /* Primero validar la cabecera */
    if (elf32_validate(
            image, size, &header
        ) != 0)
        return -1;

    const uint8_t *bytes =
        (const uint8_t *)image;

    Elf32_LoadInfo info = {0};
    info.entry = header.e_entry;

    int entry_found = 0;

    for (uint32_t i = 0; i < header.e_phnum; i++) {
        Elf32_Phdr segment;

        uint32_t offset =
            header.e_phoff +
            i * sizeof(Elf32_Phdr);

        elf_copy(
            &segment,
            bytes + offset,
            sizeof(segment)
        );

        if (segment.p_type != PT_LOAD)
            continue;

        /* Rechazar segmentos vacios */
        if (segment.p_memsz == 0U)
            return -1;

        /* Los bytes del archivo caben en memoria */
        if (segment.p_filesz > segment.p_memsz)
            return -1;

        /* Comprobar limites del archivo */
        if (segment.p_offset > size ||
            segment.p_filesz >
                size - segment.p_offset)
            return -1;

        /* Limitar direcciones virtuales */
        if (segment.p_vaddr < MIOS_ELF_USER_BASE ||
            segment.p_vaddr >= MIOS_ELF_USER_LIMIT)
            return -1;

        if (segment.p_memsz >
            MIOS_ELF_USER_LIMIT - segment.p_vaddr)
            return -1;

        /* Validar permisos conocidos */
        if ((segment.p_flags & ~0x7U) != 0U)
            return -1;

        /*
         * Validar alineacion ELF.
         * 0 y 1 significan sin restriccion.
         */
        if (segment.p_align > 1U) {
            uint32_t align = segment.p_align;

            /* Debe ser potencia de dos */
            if ((align & (align - 1U)) != 0U)
                return -1;

            if ((segment.p_vaddr & (align - 1U)) !=
                (segment.p_offset & (align - 1U)))
                return -1;
        }

        /* Rechazar segmentos superpuestos */
        for (uint32_t j = 0; j < i; j++) {
            Elf32_Phdr other;

            uint32_t other_offset =
                header.e_phoff +
                j * sizeof(Elf32_Phdr);

            elf_copy(
                &other,
                bytes + other_offset,
                sizeof(other)
            );

            if (other.p_type != PT_LOAD)
                continue;

            uint32_t a0 = segment.p_vaddr;
            uint32_t a1 = a0 + segment.p_memsz;

            uint32_t b0 = other.p_vaddr;
            uint32_t b1 = b0 + other.p_memsz;

            if (a0 < b1 && b0 < a1)
                return -1;
        }

        /*
         * La entrada debe estar respaldada
         * por bytes ejecutables del archivo.
         */
        if ((segment.p_flags & ELF_PF_X) != 0U &&
            segment.p_filesz > 0U &&
            header.e_entry >= segment.p_vaddr &&
            header.e_entry - segment.p_vaddr <
                segment.p_filesz) {
            entry_found = 1;
        }

        /* Evitar desbordamiento de contadores */
        if (UINT32_MAX - info.loaded_bytes <
                segment.p_filesz ||
            UINT32_MAX - info.memory_bytes <
                segment.p_memsz) {
            return -1;
        }

        info.loaded_bytes += segment.p_filesz;
        info.memory_bytes += segment.p_memsz;
        info.load_segments++;
    }

    if (info.load_segments == 0U ||
        !entry_found)
        return -1;

    *result = info;
    return 0;
}
