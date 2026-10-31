
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

/* Iniciar la ejecucion de las dos tareas */
int user_runtime_start(void);

/* Procesar IRQ0 desde Ring 3 */
uint32_t user_runtime_tick(uint32_t saved_esp);

/*
 * Procesar SYS_EXIT desde Ring 3.
 * Devuelve ESP del siguiente contexto,
 * o cero si terminaron todas las tareas.
 */
uint32_t user_runtime_exit(void);

/* Consultar el estado de ejecucion */
int user_runtime_active(void);

uint32_t user_runtime_irq_switches(void);
int user_runtime_error(void);
/* Cantidad de salidas por tarea */
uint32_t user_runtime_task_exits(uint32_t index);

/* Cantidad total de tareas finalizadas */
uint32_t user_runtime_total_exits(void);
#endif
