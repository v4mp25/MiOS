
#include <stdint.h>
#include "pmm.h"
#include "memory_map.h"
/* Simbolos proporcionados por linker.ld */
extern char __kernel_start[];
extern char __kernel_end[];

/* Direccion y longitud del mapa Multiboot */
extern uint32_t mios_mmap_start;
extern uint32_t mios_mmap_length;
extern uint32_t mios_mbi_address;
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
/* Numero de mapeos activos por marco fisico */
static uint16_t frame_pins[PMM_MAX_FRAMES];

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

/*
 * Verificacion conservadora de estructuras Multiboot.
 *
 * Solo permite continuar si los datos de arranque
 * que utilizamos permanecen en memoria baja,
 * protegida por PMM_SAFE_START.
 *
 * No sustituye un parser Multiboot completo.
 */

static int boot_structures_safe(void) {
    if (mios_mbi_address == 0 ||
        mios_mbi_address >= PMM_SAFE_START)
        return 0;

    if (mios_mmap_start == 0 ||
        mios_mmap_start >= PMM_SAFE_START)
        return 0;

    if (mios_mmap_length == 0 ||
        mios_mmap_length >
            PMM_SAFE_START - mios_mmap_start)
        return 0;

    const uint32_t *mbi =
        (const uint32_t *)(uintptr_t)mios_mbi_address;

    uint32_t flags = mbi[0];

    /* Linea de comandos de arranque */
    if (flags & (1U << 2)) {
        if (mbi[4] == 0 ||
            mbi[4] >= PMM_SAFE_START)
            return 0;
    }

    /* Modulos Multiboot */
    if (flags & (1U << 3)) {
        /*
         * Por ahora no admitimos modulos.
         * Evitamos liberar sus paginas por error.
         */
        if (mbi[5] != 0)
            return 0;
    }

    /* Informacion ELF del kernel */
    if (flags & (1U << 5)) {
        uint32_t num = mbi[7];
        uint32_t size = mbi[8];
        uint32_t address = mbi[9];

        if (num != 0) {
            if (size == 0 ||
                address >= PMM_SAFE_START)
                return 0;

            uint64_t bytes =
                (uint64_t)num * size;

            if (bytes >
                PMM_SAFE_START - address)
                return 0;
        }
    }

    /* Informacion de unidades */
    if (flags & (1U << 7)) {
        if (mbi[13] != 0) {
            if (mbi[14] >= PMM_SAFE_START ||
                mbi[13] >
                    PMM_SAFE_START - mbi[14])
                return 0;
        }
    }

    /* Tabla de configuracion ROM */
    if (flags & (1U << 8)) {
        if (mbi[15] >= PMM_SAFE_START)
            return 0;
    }

    /* Nombre del cargador de arranque */
    if (flags & (1U << 9)) {
        if (mbi[16] == 0 ||
            mbi[16] >= PMM_SAFE_START)
            return 0;
    }

    /* Informacion APM */
    if (flags & (1U << 10)) {
        if (mbi[17] == 0 ||
            mbi[17] >= PMM_SAFE_START)
            return 0;
    }

    /* Informacion VBE */
    if (flags & (1U << 11)) {
        if ((mbi[18] != 0 &&
             mbi[18] >= PMM_SAFE_START) ||
            (mbi[19] != 0 &&
             mbi[19] >= PMM_SAFE_START))
            return 0;
    }

    return 1;
}


int pmm_init(void) {
    pmm_ready = 0;
    total_frames = 0;
    free_frames = 0;

    if (!memory_map_available()) {
        return -1;
    }

    if (!boot_structures_safe()) {
        return -1;
    }

    /*
     * Comenzar con toda la RAM reservada.
     */
    for (uint32_t i = 0; i < PMM_BITMAP_BYTES; i++) {
        used_bitmap[i] = 0xFF;
        allocated_bitmap[i] = 0;
    }
    for (uint32_t i = 0; i < PMM_MAX_FRAMES; i++) {
    frame_pins[i] = 0;
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
    
/* Proteger la imagen completa del kernel */
uint64_t kernel_start =
    (uint32_t)(uintptr_t)__kernel_start;

uint64_t kernel_end =
    (uint32_t)(uintptr_t)__kernel_end;

if (kernel_end <= kernel_start ||
    kernel_end > PMM_LIMIT_BYTES)
    return -1;

reserve_region(
    kernel_start,
    kernel_end - kernel_start
);

/* Proteger el mapa de memoria de GRUB */
if (mios_mmap_length != 0) {
    reserve_region(
        (uint64_t)mios_mmap_start,
        (uint64_t)mios_mmap_length
    );
}


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

    /* Verificar que el PMM este inicializado */
    if (!pmm_ready) {
        return -1;
    }

    /* Verificar alineacion de 4096 bytes */
    if (address % PMM_FRAME_SIZE != 0) {
        return -1;
    }

    /* Verificar que la direccion sea valida */
    if (address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES) {
        return -1;
    }

    uint32_t index = address / PMM_FRAME_SIZE;

    /* Impedir liberar marcos no asignados */
    if (!bit_get(allocated_bitmap, index)) {
        return -1;
    }

    /* Impedir liberar memoria que sigue mapeada */
    if (frame_pins[index] != 0) {
        return -1;
    }

    /* Liberar el marco fisico */
    bit_clear(allocated_bitmap, index);
    mark_free(index);

    /* Actualizar contador de memoria libre */
    free_frames++;

    return 0;
}


int pmm_is_reserved(uint32_t address) {
    if (!pmm_ready)
        return -1;

    if (address >= PMM_LIMIT_BYTES)
        return -1;

    uint32_t index = address / PMM_FRAME_SIZE;

    return bit_get(used_bitmap, index);
}

/*
 * Comprueba que un marco de 4 KiB puede
 * utilizarse según el mapa de GRUB.
 *
 * La verificacion es independiente del bitmap.
 */
static int frame_allowed_by_map(uint32_t frame) {
    uint64_t start =
        (uint64_t)frame * PMM_FRAME_SIZE;
    uint64_t end = start + PMM_FRAME_SIZE;

    if (start < PMM_SAFE_START ||
        end > PMM_LIMIT_BYTES)
        return 0;

    int inside_usable = 0;

    for (uint32_t i = 0;
         i < memory_map_regions(); i++) {

        MemoryRegionInfo r;

        if (memory_map_get_region(i, &r) != 0)
            return 0;

        uint64_t r_end = r.base + r.length;

        if (r_end < r.base)
            r_end = UINT64_MAX;

        if (r.type == 1) {
            if (start >= r.base && end <= r_end)
                inside_usable = 1;
        } else {
            if (start < r_end && end > r.base)
                return 0;
        }
    }

    return inside_usable;
}

int pmm_validate(void) {
    if (!pmm_ready || !memory_map_available())
        return -1;

    if (!boot_structures_safe())
        return -1;

    uint64_t kernel_start =
        (uint32_t)(uintptr_t)__kernel_start;

    uint64_t kernel_end =
        (uint32_t)(uintptr_t)__kernel_end;

    if (kernel_end <= kernel_start ||
        kernel_end > PMM_LIMIT_BYTES)
        return -1;

    uint32_t free_count = 0;
    uint32_t allocated_count = 0;

    for (uint32_t i = 0;
         i < PMM_MAX_FRAMES; i++) {

        uint64_t start =
            (uint64_t)i * PMM_FRAME_SIZE;
        uint64_t end = start + PMM_FRAME_SIZE;

        int used = bit_get(used_bitmap, i);
        int allocated = bit_get(allocated_bitmap, i);

        int allowed = frame_allowed_by_map(i);

        /* Nunca permitir marcos del kernel */
        if (start < kernel_end && end > kernel_start)
            allowed = 0;

        /* Un marco asignado debe estar ocupado */
        if (allocated && !used)
            return -1;

        /* No permitir asignaciones en zonas prohibidas */
        if (allocated && !allowed)
            return -1;

        /*
         * Si es permitido y no esta asignado,
         * debe encontrarse libre.
         *
         * Si no es permitido, debe estar reservado.
         */
        if (allowed && !allocated) {
            if (used)
                return -1;
        } else {
            if (!used)
                return -1;
        }

        if (!used)
            free_count++;

        if (allocated)
            allocated_count++;
    }

    /* Comprobar la contabilidad interna */
    if (free_count != free_frames)
        return -1;

    if (free_count + allocated_count != total_frames)
        return -1;

    return 0;
}

/* Verificar si un marco fue reservado por el PMM */
int pmm_frame_allocated(uint32_t address) {
    if (!pmm_ready)
        return 0;

    if (address % PMM_FRAME_SIZE != 0)
        return 0;

    if (address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES)
        return 0;

    uint32_t index = address / PMM_FRAME_SIZE;

    return bit_get(allocated_bitmap, index);
}


/*
 * Registrar una referencia de mapeo.
 * El marco debe pertenecer al PMM.
 */
int pmm_pin_frame(uint32_t address) {
    if (!pmm_ready)
        return -1;

    if (address % PMM_FRAME_SIZE != 0 ||
        address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES)
        return -1;

    uint32_t index = address / PMM_FRAME_SIZE;

    if (!bit_get(allocated_bitmap, index))
        return -1;

    if (frame_pins[index] == UINT16_MAX)
        return -1;

    frame_pins[index]++;
    return 0;
}

/* Quitar una referencia de mapeo */
int pmm_unpin_frame(uint32_t address) {
    if (!pmm_ready)
        return -1;

    if (address % PMM_FRAME_SIZE != 0 ||
        address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES)
        return -1;

    uint32_t index = address / PMM_FRAME_SIZE;

    if (!bit_get(allocated_bitmap, index))
        return -1;

    if (frame_pins[index] == 0)
        return -1;

    frame_pins[index]--;
    return 0;
}

/* Consultar cuantas referencias tiene */
uint32_t pmm_frame_pin_count(uint32_t address) {
    if (!pmm_ready ||
        address % PMM_FRAME_SIZE != 0 ||
        address < PMM_SAFE_START ||
        address >= PMM_LIMIT_BYTES)
        return 0;

    return frame_pins[address / PMM_FRAME_SIZE];
}
