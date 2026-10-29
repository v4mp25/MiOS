
#include <stdint.h>
#include "memory_map.h"

/* Campos iniciales de la estructura Multiboot 1 */
typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t reserved[10];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} MultibootInfo;

/* Entrada minima del mapa de memoria */
typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t base;
    uint64_t length;
    uint32_t type;
} MemoryEntry;

static uint32_t region_count = 0;
uint32_t mios_mmap_start = 0;
uint32_t mios_mmap_length = 0;
static uint32_t usable_mib = 0;
static int map_valid = 0;

void memory_map_init(
   
    uint32_t magic,
    uint32_t mbi_address
) {
    region_count = 0;
    usable_mib = 0;
    map_valid = 0;
     mios_mmap_start = 0;
mios_mmap_length = 0;

    if (magic != MULTIBOOT_MAGIC ||
        mbi_address == 0)
        return;

    const MultibootInfo *mbi =
        (const MultibootInfo *)(uintptr_t)mbi_address;

    /* Bit 6: mapa de memoria disponible */
    if (!(mbi->flags & (1U << 6)))
        return;

    if (mbi->mmap_addr == 0 ||
        mbi->mmap_length < sizeof(MemoryEntry))
        return;

    /* Comprobar que el rango no desborde 32 bits */
    if (mbi->mmap_length >
        UINT32_MAX - mbi->mmap_addr)
        return;

    uintptr_t current = mbi->mmap_addr;
    uintptr_t end =
        current + mbi->mmap_length;

    uint64_t usable_bytes = 0;

    while (current < end) {
        if (end - current < sizeof(uint32_t))
            return;

        const MemoryEntry *entry =
            (const MemoryEntry *)current;

        uint32_t size = entry->size;

        if (size < 20U ||
            (uint64_t)size + 4U > end - current)
            return;

        region_count++;

        /*
         * Tipo 1: RAM disponible segun BIOS.
         * No significa que podamos sobrescribir
         * todas esas direcciones inmediatamente.
         */
        if (entry->type == 1) {
            usable_bytes += entry->length;
        }

        current += (uintptr_t)size + 4U;
    }

    if (current != end)
        return;

    uint64_t mib = usable_bytes / (1024U * 1024U);

    if (mib > UINT32_MAX)
        usable_mib = UINT32_MAX;
    else
        usable_mib = (uint32_t)mib;
mios_mmap_start = mbi->mmap_addr;
mios_mmap_length = mbi->mmap_length;
    map_valid = 1;

}

uint32_t memory_map_regions(void) {
    return region_count;
}

uint32_t memory_map_usable_mib(void) {
    return usable_mib;
}

int memory_map_available(void) {
    return map_valid;
}

int memory_map_get_region(
    uint32_t index,
    MemoryRegionInfo *out
) {
    if (!map_valid || out == 0 ||
        index >= region_count)
        return -1;

    /*
     * Usamos la direccion Multiboot guardada
     * durante la inicializacion.
     */
    extern uint32_t mios_mmap_start;
    extern uint32_t mios_mmap_length;

    if (mios_mmap_length >
        UINT32_MAX - mios_mmap_start)
        return -1;

    uintptr_t current = mios_mmap_start;
    uintptr_t end = current + mios_mmap_length;

    for (uint32_t i = 0; current < end; i++) {
        if (end - current < 4U)
            return -1;

        const MemoryEntry *entry =
            (const MemoryEntry *)current;

        if (entry->size < 20U ||
            (uint64_t)entry->size + 4U > end - current)
            return -1;

        if (i == index) {
            out->base = entry->base;
            out->length = entry->length;
            out->type = entry->type;
            return 0;
        }

        current += (uintptr_t)entry->size + 4U;
    }

    return -1;
}
