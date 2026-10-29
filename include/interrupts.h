
#ifndef MIOS_INTERRUPTS_H
#define MIOS_INTERRUPTS_H

#include <stdint.h>

/* Configura IDT, PIC y PIT */
void interrupts_init(void);

/* Devuelve los pulsos del temporizador */
uint32_t timer_get_ticks(void);

#endif

