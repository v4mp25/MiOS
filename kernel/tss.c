
#include <stdint.h>
#include <stddef.h>

#include "tss.h"
#include "gdt.h"

/* TSS clasica x86 de 32 bits: 104 bytes */
typedef struct __attribute__((packed)) {
    uint32_t previous;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} TSS32;

typedef char tss_size_check[
    sizeof(TSS32) == 104 ? 1 : -1
];

static TSS32 kernel_tss
    __attribute__((aligned(16)));

/*
 * Pila provisional para las futuras
 * entradas desde Ring 3.
 *
 * Todavia no se utiliza porque todos
 * los hilos actuales trabajan en Ring 0.
 */
static uint8_t initial_kernel_stack[16384]
    __attribute__((aligned(16)));

static int tss_prepared = 0;

void tss_prepare(void) {
    uint8_t *bytes = (uint8_t *)&kernel_tss;

    for (uint32_t i = 0; i < sizeof(TSS32); i++)
        bytes[i] = 0;

    kernel_tss.ss0 = GDT_KERNEL_DATA;

    kernel_tss.esp0 =
        (uint32_t)(uintptr_t)
        (initial_kernel_stack +
         sizeof(initial_kernel_stack));

    /*
     * Sin bitmap de permisos de E/S.
     * Las instrucciones de puerto no
     * quedaran autorizadas a Ring 3.
     */
    kernel_tss.iomap_base = sizeof(TSS32);

    tss_prepared = 1;
}

uint32_t tss_get_base(void) {
    return (uint32_t)(uintptr_t)&kernel_tss;
}

uint32_t tss_get_size(void) {
    return (uint32_t)sizeof(TSS32);
}

void tss_load(void) {
    uint16_t selector = GDT_TSS_SELECTOR;

    __asm__ volatile (
        "ltr %0"
        :
        : "r"(selector)
        : "memory"
    );
}

void tss_set_kernel_stack(uint32_t esp0) {
    kernel_tss.esp0 = esp0;
}

int tss_verify(void) {
    uint16_t tr = 0;

    __asm__ volatile (
        "str %0"
        : "=r"(tr)
    );

    return tss_prepared &&
           tr == GDT_TSS_SELECTOR &&
           kernel_tss.ss0 == GDT_KERNEL_DATA &&
           kernel_tss.esp0 != 0 &&
           kernel_tss.iomap_base == sizeof(TSS32);
}
