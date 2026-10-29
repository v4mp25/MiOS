
#ifndef MIOS_MEMORY_MAP_H
#define MIOS_MEMORY_MAP_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002U

void memory_map_init(
    uint32_t magic,
    uint32_t mbi_address
);

uint32_t memory_map_regions(void);
uint32_t memory_map_usable_mib(void);
int memory_map_available(void);

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
} MemoryRegionInfo;

/* Obtiene los datos de una region del mapa */
int memory_map_get_region(
    uint32_t index,
    MemoryRegionInfo *out
);
int memory_map_validate_boot(void);
#endif
