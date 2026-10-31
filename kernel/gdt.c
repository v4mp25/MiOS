
#include <stdint.h>
#include "gdt.h"
#include "tss.h"
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} GdtEntry;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} GdtPointer;

static GdtEntry gdt[6];
static GdtPointer gdt_pointer;

extern void gdt_load(const GdtPointer *pointer);

static void gdt_set_entry(
    int index,
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t granularity
) {
    gdt[index].base_low = base & 0xFFFFU;
    gdt[index].base_middle = (base >> 16) & 0xFFU;
    gdt[index].base_high = (base >> 24) & 0xFFU;

    gdt[index].limit_low = limit & 0xFFFFU;

    gdt[index].granularity =
        ((limit >> 16) & 0x0FU) |
        (granularity & 0xF0U);

    gdt[index].access = access;
}

void gdt_init(void) {
    gdt_pointer.limit = sizeof(gdt) - 1U;
    gdt_pointer.base = (uint32_t)(uintptr_t)&gdt[0];

    /* Descriptor nulo */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Codigo kernel Ring 0 */
    gdt_set_entry(1, 0, 0xFFFFFU, 0x9A, 0xCF);

    /* Datos kernel Ring 0 */
    gdt_set_entry(2, 0, 0xFFFFFU, 0x92, 0xCF);

    /* Codigo usuario Ring 3 */
    gdt_set_entry(3, 0, 0xFFFFFU, 0xFA, 0xCF);

    /* Datos usuario Ring 3 */
    gdt_set_entry(4, 0, 0xFFFFFU, 0xF2, 0xCF);
/* Entrada 5: TSS de 32 bits disponible */
gdt_set_entry(
    5,
    tss_get_base(),
    tss_get_size() - 1U,
    0x89,
    0x00
);
    gdt_load(&gdt_pointer);
}

int gdt_verify(void) {
    uint16_t cs, ds, ss;

    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    return cs == GDT_KERNEL_CODE &&
           ds == GDT_KERNEL_DATA &&
           ss == GDT_KERNEL_DATA;
}
