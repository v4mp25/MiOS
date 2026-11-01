
#include <stdint.h>

#include "user_copy.h"
#include "paging.h"

/*
 * Region USER actual:
 *
 * 0x10800000: codigo y datos
 * 0x10801000: pila de usuario
 *
 * Cada pagina ocupa 4096 bytes.
 */
#define USER_BEGIN 0x10800000U
#define USER_END   0x10802000U

#define PAGE_SIZE 4096U
#define PAGE_PRESENT 0x001U
#define PAGE_USER    0x004U

static uint32_t current_cr3(void) {
    uint32_t cr3;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    return cr3 & 0xFFFFF000U;
}

int user_memory_readable(
    uint32_t address,
    uint32_t length
) {
    if (!paging_cpu_enabled())
        return -1;

    /*
     * Primera version: textos pequenos.
     * Evita escrituras masivas en VGA.
     */
    if (length == 0U || length > 160U)
        return -1;

    /* Comprobar limites sin overflow */
    if (address < USER_BEGIN ||
        address >= USER_END ||
        length > USER_END - address) {
        return -1;
    }

    uint32_t cr3 = current_cr3();

    if (cr3 == paging_directory_address())
        return -1;

    /*
     * Las tablas fisicas del prototipo
     * estan accesibles por identidad en
     * los primeros 128 MiB.
     */
    uint32_t *directory =
        (uint32_t *)(uintptr_t)cr3;

    uint32_t first_page =
        address & 0xFFFFF000U;

    uint32_t last_page =
        (address + length - 1U) &
        0xFFFFF000U;

    for (uint32_t page = first_page;
         page <= last_page;
         page += PAGE_SIZE) {

        uint32_t pde_index = page >> 22;
        uint32_t pte_index =
            (page >> 12) & 0x3FFU;

        uint32_t pde = directory[pde_index];

        if ((pde & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) {
            return -1;
        }

        uint32_t *table =
            (uint32_t *)(uintptr_t)
            (pde & 0xFFFFF000U);

        uint32_t pte = table[pte_index];

        if ((pte & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) {
            return -1;
        }
    }

    return 0;
}
