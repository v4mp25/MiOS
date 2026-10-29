
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
