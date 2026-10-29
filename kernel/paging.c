
#include <stdint.h>
#include "paging.h"

/*
 * MiOS - Preparacion de paginacion x86
 *
 * Mapeo de identidad de 128 MiB.
 * Todavia NO se modifica CR0 ni CR3.
 */

#define PAGE_PRESENT 0x001U
#define PAGE_WRITE   0x002U

#define PAGE_TABLE_COUNT 32U
#define PAGE_ENTRIES 1024U

static uint32_t page_directory[1024]
    __attribute__((aligned(4096)));

static uint32_t page_tables
    [PAGE_TABLE_COUNT][PAGE_ENTRIES]
    __attribute__((aligned(4096)));

static int initialized = 0;

int paging_init(void) {
    initialized = 0;

    for (uint32_t i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    for (uint32_t table = 0;
         table < PAGE_TABLE_COUNT;
         table++) {

        for (uint32_t page = 0;
             page < PAGE_ENTRIES;
             page++) {

            uint32_t frame =
                table * PAGE_ENTRIES + page;

            uint32_t address =
                frame * PAGE_SIZE;

            page_tables[table][page] =
                address | PAGE_PRESENT | PAGE_WRITE;
        }

        page_directory[table] =
            (uint32_t)(uintptr_t)&page_tables[table][0]
            | PAGE_PRESENT | PAGE_WRITE;
    }

    initialized = 1;
    return 0;
}

int paging_is_mapped(uint32_t address) {
    if (!initialized)
        return 0;

    uint32_t directory_index = address >> 22;
    uint32_t table_index =
        (address >> 12) & 0x3FFU;

    if (directory_index >= PAGE_TABLE_COUNT)
        return 0;

    if (!(page_directory[directory_index]
          & PAGE_PRESENT))
        return 0;

    return (page_tables[directory_index][table_index]
            & PAGE_PRESENT) != 0;
}

uint32_t paging_mapped_mib(void) {
    if (!initialized)
        return 0;

    return PAGE_TABLE_COUNT * 4U;
}

uint32_t paging_directory_address(void) {
    if (!initialized)
        return 0;

    return (uint32_t)(uintptr_t)page_directory;
}

int paging_ready(void) {
    return initialized;
}

/*
 * Consultar si la CPU tiene paginacion activa.
 * CR0 bit 31 = PG.
 */
int paging_cpu_enabled(void) {
    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    return (cr0 & 0x80000000U) != 0;
}

/*
 * Activar paginacion x86 de 32 bits.
 *
 * Requisitos:
 * - Tablas inicializadas.
 * - Directorio alineado a 4096 bytes.
 * - Mapeo de identidad para el kernel.
 * - Pila, IDT y estructuras activas accesibles.
 */
int paging_enable(void) {
    if (!initialized)
        return -1;

    uint32_t directory =
        (uint32_t)(uintptr_t)page_directory;

    if ((directory & 0xFFFU) != 0)
        return -1;

    if (directory >= 128U * 1024U * 1024U)
        return -1;

    extern char __kernel_end[];

    const uint32_t limit = 128U * 1024U * 1024U;

    uint32_t kernel_end =
        (uint32_t)(uintptr_t)__kernel_end;

    uint32_t stack_address;

    __asm__ volatile (
        "mov %%esp, %0"
        : "=r"(stack_address)
    );

    struct __attribute__((packed)) {
        uint16_t size;
        uint32_t address;
    } idtr;

    __asm__ volatile (
        "sidt %0"
        : "=m"(idtr)
    );

    if (kernel_end >= limit ||
        stack_address >= limit ||
        idtr.address >= limit)
        return -1;

    if ((uint64_t)idtr.address +
        (uint64_t)idtr.size >= limit)
        return -1;

    for (uint32_t i = 0;
         i < PAGE_TABLE_COUNT; i++) {

        uint32_t table_address =
            (uint32_t)(uintptr_t)&page_tables[i][0];

        if ((table_address & 0xFFFU) != 0 ||
            (uint64_t)table_address + PAGE_SIZE > limit)
            return -1;
    }

    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    /* No reemplazar una paginacion ya activa */
    if (cr0 & 0x80000000U)
        return -1;

    /* La paginacion requiere modo protegido */
    if (!(cr0 & 0x00000001U))
        return -1;

    uint32_t flags;

    /* Guardar IF y deshabilitar interrupciones */
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );

    /*
     * CR3 recibe la direccion fisica del
     * directorio de paginas.
     *
     * Antes de activar PG, nuestra direccion
     * lineal coincide con la fisica.
     */
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(directory)
        : "memory"
    );

    /*
     * Activar bit PG (31) de CR0.
     * El salto inmediato continua la ejecucion
     * con el mapeo de identidad.
     */
    __asm__ volatile (
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        "jmp 1f\n\t"
        "1:"
        :
        :
        : "eax", "memory"
    );

    /* Restaurar el estado anterior de IF */
    if (flags & (1U << 9)) {
        __asm__ volatile ("sti" ::: "memory");
    }

    return paging_cpu_enabled() ? 0 : -1;
}
