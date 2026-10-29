
#include <stdint.h>
#include <stddef.h>
#include "memory.h"

/* 64 paginas de 4096 bytes = 256 KiB */
static uint8_t memory_pool
    [MIOS_PAGE_COUNT][MIOS_PAGE_SIZE]
    __attribute__((aligned(4096)));

/* 0 = libre, 1 = ocupada */
static uint8_t page_used[MIOS_PAGE_COUNT];

static uint32_t used_count = 0;

void memory_init(void) {
    for (uint32_t i = 0; i < MIOS_PAGE_COUNT; i++) {
        page_used[i] = 0;
    }
    used_count = 0;
}

void *memory_alloc_page(void) {
    for (uint32_t i = 0; i < MIOS_PAGE_COUNT; i++) {
        if (page_used[i] == 0) {
            page_used[i] = 1;
            used_count++;
            return &memory_pool[i][0];
        }
    }

    return NULL;
}

int memory_free_page(void *ptr) {
    if (ptr == NULL)
        return -1;

    uintptr_t start = (uintptr_t)&memory_pool[0][0];
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t size = sizeof(memory_pool);

    if (address < start || address - start >= size)
        return -1;

    uintptr_t offset = address - start;

    /* Solo aceptar el inicio exacto de una pagina */
    if (offset % MIOS_PAGE_SIZE != 0)
        return -1;

    uint32_t index = (uint32_t)(offset / MIOS_PAGE_SIZE);

    if (page_used[index] == 0)
        return -1;

    page_used[index] = 0;
    used_count--;

    return 0;
}

uint32_t memory_total_pages(void) {
    return MIOS_PAGE_COUNT;
}

uint32_t memory_used_pages(void) {
    return used_count;
}

uint32_t memory_free_pages(void) {
    return MIOS_PAGE_COUNT - used_count;
}
