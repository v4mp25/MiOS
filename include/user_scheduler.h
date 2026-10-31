
#ifndef MIOS_USER_SCHEDULER_H
#define MIOS_USER_SCHEDULER_H

#include <stdint.h>

/*
 * Planificador experimental Round Robin
 * de tareas Ring 3.
 *
 * Todavia no cambia el contexto de CPU.
 */
void user_rr_reset(void);

/* Devuelve el indice de la siguiente tarea */
int user_rr_next(void);

/* Consultar tarea seleccionada */
int user_rr_current(void);

/* Numero de cambios de seleccion */
uint32_t user_rr_switches(void);

/* Verificar una secuencia de alternancia */
int user_rr_selftest(void);

#endif
