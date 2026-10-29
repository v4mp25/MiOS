
#ifndef MIOS_PMM_H
#define MIOS_PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE 4096U

/* Inicializacion del administrador */
int pmm_init(void);

/* Consulta de marcos */
uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_used_frames(void);

/* Reserva y liberacion de marcos fisicos */
int pmm_alloc_frame(uint32_t *address);
int pmm_free_frame(uint32_t address);

#endif
