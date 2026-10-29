
#ifndef MIOS_MEMORY_H
#define MIOS_MEMORY_H

#include <stdint.h>

#define MIOS_PAGE_SIZE 4096U
#define MIOS_PAGE_COUNT 64U

void memory_init(void);
void *memory_alloc_page(void);
int memory_free_page(void *ptr);

uint32_t memory_total_pages(void);
uint32_t memory_used_pages(void);
uint32_t memory_free_pages(void);

#endif

