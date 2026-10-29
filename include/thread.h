
#ifndef MIOS_THREAD_H
#define MIOS_THREAD_H

#include <stdint.h>

#define MIOS_MAX_THREADS 2

void thread_init(void);
int thread_create(void (*function)(void));
void thread_yield(void);
int thread_run(void);
uint32_t thread_count(void);

/* Funciones usadas por las interrupciones */
uint32_t scheduler_on_tick(uint32_t esp);
uint32_t scheduler_on_yield(uint32_t esp);
/* Cancelar un hilo que aun no ha empezado */
int thread_destroy(int tid);
#endif

