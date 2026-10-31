
#ifndef MIOS_USER_CONTEXT_H
#define MIOS_USER_CONTEXT_H

#include <stdint.h>

/*
 * Contexto de usuario capturado
 * durante una interrupcion IRQ0.
 */
typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    uint32_t user_esp;
    uint32_t user_ss;

    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;

    uint32_t cr3;
} UserContext;

void user_context_reset(void);

/*
 * Recibe el ESP del marco guardado
 * por el controlador IRQ0 de Ring 3.
 */
void user_context_capture(
    const uint32_t *frame
);

uint32_t user_context_count(void);

int user_context_verify(uint32_t expected_cr3);

#endif
