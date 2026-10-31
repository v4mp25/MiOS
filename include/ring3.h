
#ifndef MIOS_RING3_H
#define MIOS_RING3_H

#include <stdint.h>

/*
 * Entra a Ring 3 usando el CR3 indicado.
 * Regresa solamente cuando SYS_EXIT
 * recupera el contexto del kernel.
 */
void ring3_enter(
    uint32_t user_cr3,
    uint32_t user_esp
);

/*
 * Entrar en Ring 3 con una direccion
 * de inicio indicada por el ELF.
 */
void ring3_enter_at(
    uint32_t user_cr3,
    uint32_t user_esp,
    uint32_t user_entry
);

#endif
