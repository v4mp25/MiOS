
#include <stdint.h>
#include "pmm.h"
#include "memory_map.h"

/*
 * MiOS - Administrador de memoria fisica
 *
 * Primera version:
 * - x86 de 32 bits
 * - hasta 128 MiB
 * - marcos de 4096 bytes
 * - bitmap de marcos disponibles
 * - bitmap de marcos asignados
 *
 * No escribe directamente en los marcos.
 */

#define PMM_LIMIT_BYTES (128U * 1024U * 1024U)
#define PMM_MAX_FRAMES  (PMM_LIMIT_BYTES / PMM_FRAME_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_FRAMES / 8U)

/* Proteccion conservadora de los primeros 4 MiB */
#define PMM_SAFE_START 0x00400000U

static uint8_t used_bitmap[PMM_BITMAP_BYTES];
static uint8_t allocated_bitmap[PMM_BITMAP_BYTES];

static uint32_t total_frames = 0;
static uint32_t free_frames = 0;
static int pmm_ready = 0;

static void bit_set(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8U] |=
        (uint8_t)(1U << (index % 8U));
}

static void bit_clear(uint8_t *bitmap, uint32_t index) {
    bitmap[index / 8U] &=
        (uint8_t)~(1U << (index % 8U));
}

static int bit_get(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8U] >>
            (index % 8U)) & 1U;
}

/* Solo registra un marco como disponible */
static void mark_free(uint32_t index) {
    if (index >= PMM_MAX_FRAMES)
        return;

    bit_clear(used_bitmap, index);
}

/* Registra un marco como reservado */
static void mark_reserved(uint32_t index) {
    if (index >= PMM_MAX_FRAMES)
        return;

    bit_set(used_bitmap, index);
}

/*
 * Marca marcos contenidos completamente en
 * una region utilizable.
 */
static void add_usable_region(
    uint64_t base,
    uint64_t length
) {
    if (length == 0)
        return;

    uint64_t end = base + length;

    if (end < base)
        return;

    if (base < PMM_SAFE_START)
        base = PMM_SAFE_START;

    if (end > PMM_LIMIT_BYTES)
        end = PMM_LIMIT_BYTES;

    if (base >= end)
        return;

    uint64_t first =
        (base + PMM_FRAME_SIZE - 1U) /
        PMM_FRAME_SIZE;

    uint64_t last = end / PMM_FRAME_SIZE;

    for (uint64_t i = first; i < last; i++)
        mark_free((uint32_t)i);
}

/*
 * Reserva cualquier marco que interseque
 * una region no utilizable.
 */
static void reserve_region(
    uint64_t base,
    uint64_t length
) {
    if (length == 0)
        return;

    uint64_t end = base + length;

    if (end < base)
        end = UINT64_MAX;

    if (base >= PMM_LIMIT_BYTES)
        return;

    if (end > PMM_LIMIT_BYTES)
        end = PMM_LIMIT_BYTES;

    uint64_t first = base / PMM_FRAME_SIZE;

    uint64_t last =
        (end / PMM_FRAME_SIZE) +
        ((end % PMM_FRAME_SIZE) != 0);

    for (uint64_t i = first; i < last; i++)
        mark_reserved((uint32_t)i);
}

int pmm_init(void) {
    pmm_ready = 0;
    total_frames = 0;
    free_frames = 0;

    if (!memory_map_available())
        return -1;

    /*
     * Comenzar con toda la RAM reservada.
     */
    for (uint32_t i = 0; i < PMM_BITMAP_BYTES; i++) {
        used_bitmap[i] = 0xFF;
        allocated_bitmap[i] = 0;
    }

    /*
     * Primera pasada:
     * desbloquear regiones tipo 1.
     */
    for (uint32_t i = 0; i < memory_map_regions(); i++) {
        MemoryRegionInfo region;

        if (memory_map_get_region(i, &region) != 0)
            return -1;

        if (region.type == 1) {
            add_usable_region(
                region.base,
                region.length
            );
        }
    }

    /*
     * Segunda pasada:
     * las regiones reservadas tienen prioridad.
     */
    for (uint32_t i = 0; i < memory_map_regions(); i++) {
        MemoryRegionInfo region;

        if (memory_map_get_region(i, &region) != 0)
            return -1;

        if (region.type != 1) {
            reserve_region(
                region.base,
                region.length
            );
        }
    }

    /*
     * Reservar explicitamente los primeros 4 MiB,
     * incluyendo el kernel actual, GRUB y memoria baja.
     */
    reserve_region(0, PMM_SAFE_START);

    /*
     * Contar los marcos administrables.
     */
    for (uint32_t i = 0; i < PMM_MAX_FRAMES; i++) {
        if (!bit_get(used_bitmap, i))
            free_frames++;
    }

    total_frames = free_frames;
    pmm_ready = 1;

    return 0;
}

uint32_t pmm_total_frames(void) {
    return total_frames;
}

uint32_t pmm_free_frames(void) {
    return free_frames;
}

uint32_t pmm_used_frames(void) {
    return total_frames - free_frames;
}

int pmm_alloc_frame(uint32_t *address) {
    if (!pmm_ready || address == 0)
        return -1;

    for (uint32_t i = PMM_SAFE_START / PMM_FRAME_SIZE;
         i < PMM_MAX_FRAMES;
         i++) {

        if (!bit_get(used_bitmap, i)) {
            mark_reserved(i);
            bit_set(allocated_bitmap, i);

            free_frames--;

            *address = i * PMM_FRAME_SIZE;

            return 0;
        }
    }

    return -1;
}

int pmm_free_frame(uint32_t address) {
    if (!pmm_ready)
        return -1;

    if (address % PMM_FRAME_SIZE != 0)
        return -1;

    if (address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES)
        return -1;

    uint32_t index = address / PMM_FRAME_SIZE;

    /*
     * Impedir liberar marcos reservados,
     * inexistentes o ya liberados.
     */
    if (!bit_get(allocated_bitmap, index))
        return -1;

    bit_clear(allocated_bitmap, index);
    mark_free(index);
    free_frames++;

    return 0;
}
