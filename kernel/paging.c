
#include <stdint.h>
#include "paging.h"
#include "pmm.h"
#include "irq.h"
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

/*
 * MiOS - Primer mapeo virtual dinamico.
 *
 * Utiliza una tabla reservada exclusivamente
 * para la direccion virtual 0x10000000.
 *
 * No modifica el mapeo original de 128 MiB.
 */

#define TEST_DIRECTORY_INDEX 64U
#define PHYSICAL_LIMIT (128U * 1024U * 1024U)

static uint32_t test_page_table[1024]
    __attribute__((aligned(4096)));

static int test_page_mapped = 0;

/* Recargar CR3 para invalidar traducciones */
static void paging_flush_tlb(void) {
    uint32_t cr3;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(cr3)
    );

    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(cr3)
        : "memory"
    );
}


int paging_test_map(uint32_t physical_frame) {
    uint32_t flags = irq_save();

    if (!initialized ||
        !paging_cpu_enabled() ||
        test_page_mapped ||
        (physical_frame & 0xFFFU) != 0 ||
        physical_frame < 0x00400000U ||
        physical_frame >= PHYSICAL_LIMIT ||
        page_directory[TEST_DIRECTORY_INDEX] != 0 ||
        !pmm_frame_allocated(physical_frame)) {

        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        test_page_table[i] = 0;
    }

    if (pmm_pin_frame(physical_frame) != 0) {
        irq_restore(flags);
        return -1;
    }

    test_page_table[0] =
        physical_frame | PAGE_PRESENT | PAGE_WRITE;

    page_directory[TEST_DIRECTORY_INDEX] =
        ((uint32_t)(uintptr_t)&test_page_table[0])
        | PAGE_PRESENT | PAGE_WRITE;

    paging_flush_tlb();
    test_page_mapped = 1;

    irq_restore(flags);
    return 0;
}



int paging_test_unmap(void) {
    uint32_t flags = irq_save();

    if (!initialized ||
        !paging_cpu_enabled() ||
        !test_page_mapped ||
        !(test_page_table[0] & PAGE_PRESENT)) {

        irq_restore(flags);
        return -1;
    }

    uint32_t physical_frame =
        test_page_table[0] & 0xFFFFF000U;

    if (pmm_frame_pin_count(physical_frame) == 0) {
        irq_restore(flags);
        return -1;
    }

    test_page_table[0] = 0;
    page_directory[TEST_DIRECTORY_INDEX] = 0;

    paging_flush_tlb();

    test_page_mapped = 0;

    int result = pmm_unpin_frame(physical_frame);

    irq_restore(flags);
    return result;
}


/*
 * MiOS - VMM experimental de cuatro paginas.
 *
 * Usa el indice 65 del directorio, separado
 * del indice 64 de vmmtest.
 */

#define VMM_DIRECTORY_INDEX 65U

static uint32_t vmm_table[1024]
    __attribute__((aligned(4096)));

static int vmm_initialized = 0;

/*
 * Preparar una tabla privada para esta region.
 * Solo debe utilizarse con paginacion activada.
 */
static int vmm_prepare(void) {
    if (!paging_cpu_enabled() || !initialized)
        return -1;

    if (vmm_initialized)
        return 0;

    if (page_directory[VMM_DIRECTORY_INDEX] != 0)
        return -1;

    for (uint32_t i = 0; i < 1024; i++)
        vmm_table[i] = 0;

    page_directory[VMM_DIRECTORY_INDEX] =
        ((uint32_t)(uintptr_t)&vmm_table[0])
        | PAGE_PRESENT | PAGE_WRITE;

    paging_flush_tlb();

    vmm_initialized = 1;
    return 0;
}

/* Invalidar una direccion virtual concreta */
static void vmm_invalidate(uint32_t address) {
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(address)
        : "memory"
    );
}

/* Mapear una de las cuatro paginas */

int vmm_map_page(uint32_t index, uint32_t physical_frame) {
    uint32_t flags = irq_save();

    if (!initialized ||
        !paging_cpu_enabled() ||
        index >= MIOS_VMM_PAGES ||
        (physical_frame & 0xFFFU) != 0 ||
        physical_frame < 0x00400000U ||
        physical_frame >= 128U * 1024U * 1024U) {

        irq_restore(flags);
        return -1;
    }

    if (!pmm_frame_allocated(physical_frame)) {
        irq_restore(flags);
        return -1;
    }

    if (vmm_prepare() != 0) {
        irq_restore(flags);
        return -1;
    }

    if (vmm_table[index] & PAGE_PRESENT) {
        irq_restore(flags);
        return -1;
    }

    /* Impedir duplicados en nuestra region VMM */
    for (uint32_t i = 0; i < MIOS_VMM_PAGES; i++) {
        if ((vmm_table[i] & PAGE_PRESENT) &&
            (vmm_table[i] & 0xFFFFF000U) == physical_frame) {

            irq_restore(flags);
            return -1;
        }
    }

    /* Registrar la referencia del marco */
    if (pmm_pin_frame(physical_frame) != 0) {
        irq_restore(flags);
        return -1;
    }

    /* Crear el mapeo virtual */
    vmm_table[index] =
        physical_frame | PAGE_PRESENT | PAGE_WRITE;

    /* Invalidar la traduccion anterior */
    vmm_invalidate(MIOS_VMM_BASE + index * PAGE_SIZE);

    irq_restore(flags);
    return 0;
}


/* Retirar un mapeo sin liberar su marco fisico */


int vmm_unmap_page(uint32_t index) {
    uint32_t flags = irq_save();

    if (!initialized ||
        !paging_cpu_enabled() ||
        !vmm_initialized ||
        index >= MIOS_VMM_PAGES) {

        irq_restore(flags);
        return -1;
    }

    if (!(vmm_table[index] & PAGE_PRESENT)) {
        irq_restore(flags);
        return -1;
    }

    uint32_t physical_frame =
        vmm_table[index] & 0xFFFFF000U;

    if (pmm_frame_pin_count(physical_frame) == 0) {
        irq_restore(flags);
        return -1;
    }

    /* Retirar el mapeo antes de liberar su referencia */
    vmm_table[index] = 0;

    vmm_invalidate(MIOS_VMM_BASE + index * PAGE_SIZE);

    int result = pmm_unpin_frame(physical_frame);

    irq_restore(flags);
    return result;
}


/* Consultar si una pagina esta mapeada */
int vmm_page_mapped(uint32_t index) {
    if (!vmm_initialized ||
        index >= MIOS_VMM_PAGES)
        return 0;

    return (vmm_table[index] & PAGE_PRESENT) != 0;
}

/*
 * MiOS - Proteccion de paginas virtuales.
 * Requiere paginacion activa.
 */

/* Comprobar CR0.WP: bit 16 */
int paging_write_protect_enabled(void) {
    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    return (cr0 & (1U << 16)) != 0;
}

/* Hacer que el kernel respete paginas de solo lectura */
int paging_enable_write_protect(void) {
    if (!paging_cpu_enabled())
        return -1;

    uint32_t cr0;

    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );

    cr0 |= (1U << 16);

    __asm__ volatile (
        "mov %0, %%cr0"
        :
        : "r"(cr0)
        : "memory"
    );

    return paging_write_protect_enabled() ? 0 : -1;
}

/*
 * writable = 1: lectura y escritura
 * writable = 0: solo lectura
 *
 * Se utiliza la region experimental VMM.
 */

int vmm_set_writable(uint32_t index, int writable) {
    uint32_t flags = irq_save();

    if (!paging_cpu_enabled() ||
        !vmm_initialized ||
        index >= MIOS_VMM_PAGES ||
        !(vmm_table[index] & PAGE_PRESENT)) {

        irq_restore(flags);
        return -1;
    }

    if (writable) {
        vmm_table[index] |= PAGE_WRITE;
    } else {
        vmm_table[index] &= ~PAGE_WRITE;
    }

    vmm_invalidate(MIOS_VMM_BASE + index * PAGE_SIZE);

    irq_restore(flags);
    return 0;
}

/*
 * MiOS - Prueba de espacios de direcciones.
 *
 * Crea dos directorios de paginas diferentes.
 * Ambos comparten los primeros 128 MiB del
 * kernel, pero tienen una pagina privada
 * distinta en la direccion 0x10800000.
 *
 * NO modifica CR3.
 */

#define PRIVATE_PDE_INDEX 66U

static void zero_page(uint32_t *page) {
    for (uint32_t i = 0; i < 1024; i++) {
        page[i] = 0;
    }
}

int paging_address_spaces_test(void) {
    if (!initialized || !paging_cpu_enabled())
        return -1;

    uint32_t frames[6] = {0};
    uint32_t count = 0;
    int result = -1;

    /*
     * Reservar:
     * 0 = Directorio A
     * 1 = Tabla privada A
     * 2 = Marco de datos A
     * 3 = Directorio B
     * 4 = Tabla privada B
     * 5 = Marco de datos B
     */
    for (uint32_t i = 0; i < 6; i++) {
        if (pmm_alloc_frame(&frames[i]) != 0)
            goto cleanup;

        count++;
    }

    /*
     * En MiOS los primeros 128 MiB tienen
     * mapeo de identidad. Por eso podemos
     * acceder a estos marcos por su direccion
     * fisica mientras usamos el CR3 principal.
     */
    uint32_t *dir_a =
        (uint32_t *)(uintptr_t)frames[0];

    uint32_t *table_a =
        (uint32_t *)(uintptr_t)frames[1];

    uint32_t *dir_b =
        (uint32_t *)(uintptr_t)frames[3];

    uint32_t *table_b =
        (uint32_t *)(uintptr_t)frames[4];

    zero_page(dir_a);
    zero_page(table_a);
    zero_page(dir_b);
    zero_page(table_b);

    /*
     * Compartir el mapeo del kernel.
     * Por ahora son las entradas 0 a 31.
     */
    for (uint32_t i = 0; i < PAGE_TABLE_COUNT; i++) {
        dir_a[i] = page_directory[i];
        dir_b[i] = page_directory[i];
    }

    /*
     * Misma direccion virtual, diferentes
     * marcos fisicos.
     *
     * No habilitamos acceso Ring 3 aun.
     */
    table_a[0] =
        frames[2] | PAGE_PRESENT | PAGE_WRITE;

    table_b[0] =
        frames[5] | PAGE_PRESENT | PAGE_WRITE;

    dir_a[PRIVATE_PDE_INDEX] =
        frames[1] | PAGE_PRESENT | PAGE_WRITE;

    dir_b[PRIVATE_PDE_INDEX] =
        frames[4] | PAGE_PRESENT | PAGE_WRITE;

    /*
     * Comprobaciones:
     * - Directorios distintos.
     * - Marcos privados distintos.
     * - Entradas privadas presentes.
     * - Kernel compartido.
     */
    if (frames[0] == frames[3] ||
        frames[2] == frames[5])
        goto cleanup;

    if ((dir_a[PRIVATE_PDE_INDEX] & 0xFFFFF000U)
            != frames[1])
        goto cleanup;

    if ((dir_b[PRIVATE_PDE_INDEX] & 0xFFFFF000U)
            != frames[4])
        goto cleanup;

    if ((table_a[0] & 0xFFFFF000U) != frames[2])
        goto cleanup;

    if ((table_b[0] & 0xFFFFF000U) != frames[5])
        goto cleanup;

    for (uint32_t i = 0; i < PAGE_TABLE_COUNT; i++) {
        if (dir_a[i] != page_directory[i] ||
            dir_b[i] != page_directory[i])
            goto cleanup;
    }

    result = 0;

cleanup:
    /*
     * Nunca activamos estos directorios:
     * podemos retirar la prueba y devolver
     * sus marcos al PMM.
     */
    for (uint32_t i = 0; i < count; i++) {
        if (pmm_free_frame(frames[i]) != 0)
            result = -1;
    }

    return result;
}
