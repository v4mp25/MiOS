
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
uint32_t mios_mbi_address = 0;
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
     mios_mbi_address = 0;
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
mios_mbi_address = mbi_address;
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

/*
 * Validacion conservadora de Multiboot 1.
 *
 * Comprueba que las estructuras conocidas
 * permanezcan completamente dentro de
 * los primeros 4 MiB reservados por MiOS.
 */

extern uint32_t mios_mbi_address;

#define BOOT_SAFE_LIMIT 0x00400000U

static int boot_range_safe(
    uint32_t address,
    uint64_t length
) {
    if (address == 0 || length == 0)
        return 0;

    if (address >= BOOT_SAFE_LIMIT)
        return 0;

    return length <=
        (uint64_t)(BOOT_SAFE_LIMIT - address);
}

/* Comprobar una cadena terminada en cero */
static int boot_string_safe(uint32_t address) {
    if (!boot_range_safe(address, 1))
        return 0;

    const char *text =
        (const char *)(uintptr_t)address;

    uint32_t limit =
        BOOT_SAFE_LIMIT - address;

    /* Limite adicional contra cadenas corruptas */
    if (limit > 4096U)
        limit = 4096U;

    for (uint32_t i = 0; i < limit; i++) {
        if (text[i] == '\0')
            return 1;
    }

    return 0;
}

int memory_map_validate_boot(void) {
    if (!memory_map_available())
        return -1;

    /*
     * Multiboot 1: estructura principal.
     * Reservamos el rango de los campos
     * estandar hasta la informacion framebuffer.
     */
    if (!boot_range_safe(mios_mbi_address, 116U))
        return -1;

    const uint32_t *mbi =
        (const uint32_t *)(uintptr_t)mios_mbi_address;

    uint32_t flags = mbi[0];

    /* El mapa debe estar presente */
    if (!(flags & (1U << 6)))
        return -1;

    if (!boot_range_safe(
            mios_mmap_start,
            mios_mmap_length))
        return -1;

    /* Cadena de comandos */
    if (flags & (1U << 2)) {
        if (!boot_string_safe(mbi[4]))
            return -1;
    }

    /* Modulos: no admitidos en esta etapa */
    if (flags & (1U << 3)) {
        if (mbi[5] != 0)
            return -1;
    }

    /* Tablas a.out y ELF son alternativas */
    if ((flags & (1U << 4)) &&
        (flags & (1U << 5)))
        return -1;

    /* Simbolos a.out */
    if (flags & (1U << 4)) {
        uint64_t bytes =
            (uint64_t)mbi[7] + mbi[8];

        if (bytes != 0 &&
            !boot_range_safe(mbi[9], bytes))
            return -1;
    }

    /* Tabla de secciones ELF */
    if (flags & (1U << 5)) {
        uint32_t count = mbi[7];
        uint32_t size = mbi[8];
        uint32_t address = mbi[9];

        if (count != 0) {
            if (size == 0)
                return -1;

            uint64_t bytes =
                (uint64_t)count * size;

            if (!boot_range_safe(address, bytes))
                return -1;
        }
    }

    /* Informacion de unidades */
    if (flags & (1U << 7)) {
        if (mbi[13] != 0 &&
            !boot_range_safe(mbi[14], mbi[13]))
            return -1;
    }

    /* Tabla de configuracion ROM */
    if (flags & (1U << 8)) {
        if (!boot_range_safe(mbi[15], 1))
            return -1;
    }

    /* Nombre del cargador */
    if (flags & (1U << 9)) {
        if (!boot_string_safe(mbi[16]))
            return -1;
    }

    /* Estructura APM */
    if (flags & (1U << 10)) {
        if (!boot_range_safe(mbi[17], 20U))
            return -1;
    }

    /* Estructuras de informacion VBE */
    if (flags & (1U << 11)) {
        if (!boot_range_safe(mbi[18], 512U))
            return -1;

        if (!boot_range_safe(mbi[19], 256U))
            return -1;
    }

    /*
     * Bit 12: informacion de framebuffer.
     * El hardware framebuffer puede estar
     * fuera de la RAM ordinaria.
     *
     * No lo desreferenciamos ni lo tratamos
     * como una cadena o tabla en RAM.
     */

    return 0;
}

