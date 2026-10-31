
#ifndef MIOS_ELF32_H
#define MIOS_ELF32_H

#include <stdint.h>

#define ELF32_MAGIC0 0x7FU
#define ELF32_MAGIC1 'E'
#define ELF32_MAGIC2 'L'
#define ELF32_MAGIC3 'F'

#define ELFCLASS32   1U
#define ELFDATA2LSB  1U

#define ET_EXEC      2U
#define EM_386       3U
#define EV_CURRENT   1U

#define ELF32_EHDR_SIZE 52U
#define ELF32_PHDR_SIZE 32U

#define PT_LOAD 1U

/*
 * Cabecera ELF de 32 bits.
 * Debe ocupar exactamente 52 bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

/*
 * Encabezado de programa ELF32.
 * Lo utilizaremos en 4.12B.
 */
typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/*
 * Validar la cabecera y la tabla
 * de encabezados de programa.
 *
 * image: bytes del archivo ELF
 * size: tamaño real disponible
 * result: copia de la cabecera validada
 *
 * 0: valido estructuralmente
 * -1: invalido
 */
int elf32_validate(
    const void *image,
    uint32_t size,
    Elf32_Ehdr *result
);

/* Permisos declarados por los segmentos ELF */
#define ELF_PF_X 0x1U
#define ELF_PF_W 0x2U
#define ELF_PF_R 0x4U

/*
 * Region admitida por el prototipo
 * actual de memoria USER de MiOS.
 */
#define MIOS_ELF_USER_BASE  0x10800000U
#define MIOS_ELF_USER_LIMIT 0x10801000U

typedef struct {
    uint32_t load_segments;
    uint32_t entry;
    uint32_t loaded_bytes;
    uint32_t memory_bytes;
} Elf32_LoadInfo;

/*
 * Validar los segmentos cargables.
 *
 * No copia ni ejecuta el ELF.
 */
int elf32_validate_segments(
    const void *image,
    uint32_t size,
    Elf32_LoadInfo *result
);

/*
 * Cargar un ELF32 validado en la pagina
 * de codigo/datos de un espacio USER.
 *
 * address_space_id: ID devuelto por as_create()
 * entry: recibe la direccion de entrada
 *
 * Devuelve 0 si tuvo exito, -1 si fallo.
 *
 * Solo admite el rango de una pagina
 * definido en MIOS_ELF_USER_BASE/LIMIT.
 */
int elf32_load(
    const void *image,
    uint32_t size,
    uint32_t address_space_id,
    uint32_t *entry
);

#endif
