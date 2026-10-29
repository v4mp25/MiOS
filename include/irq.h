
#ifndef MIOS_IRQ_H
#define MIOS_IRQ_H

#include <stdint.h>

#define IRQ_FLAG_IF (1U << 9)

/*
 * Guardar EFLAGS y deshabilitar
 * interrupciones enmascarables.
 */
static inline uint32_t irq_save(void) {
    uint32_t flags;

    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory", "cc"
    );

    return flags;
}

/*
 * Restaurar el estado original de
 * interrupciones enmascarables.
 */
static inline void irq_restore(uint32_t flags) {
    if (flags & IRQ_FLAG_IF) {
        __asm__ volatile (
            "sti"
            :
            :
            : "memory"
        );
    } else {
        __asm__ volatile (
            "cli"
            :
            :
            : "memory"
        );
    }
}

/* Consultar si las interrupciones estan habilitadas */
static inline int irq_enabled(void) {
    uint32_t flags;

    __asm__ volatile (
        "pushfl\n\t"
        "popl %0"
        : "=r"(flags)
        :
        : "memory"
    );

    return (flags & IRQ_FLAG_IF) != 0;
}

#endif
