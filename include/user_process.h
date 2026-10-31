
#ifndef MIOS_USER_PROCESS_H
#define MIOS_USER_PROCESS_H

#include <stdint.h>
#include "user_tasks.h"

/* Estados visibles de los procesos */
#define USER_PROCESS_FREE    0U
#define USER_PROCESS_READY   1U
#define USER_PROCESS_DONE    2U

/*
 * Registro de procesos de usuario.
 * Por ahora utiliza las dos tareas
 * disponibles de MiOS.
 */
typedef struct {
    uint32_t pid;
    uint32_t task_index;
    uint32_t address_space_id;
    uint32_t cr3;
    uint32_t used;
    uint32_t state;
} UserProcess;

/* Crear registros para dos tareas USER */
int user_process_prepare(void);

/* Consultar un proceso por PID */
int user_process_find(
    uint32_t pid,
    UserProcess *result
);

/* Contar procesos registrados */
uint32_t user_process_count(void);

/* Verificar identidad y aislamiento */
int user_process_verify(void);

/* Liberar los procesos de prueba */
int user_process_cleanup(void);

/* Actualizar estados desde las tareas */
int user_process_sync_states(void);

/* Consultar un registro por posicion */
int user_process_get_at(
    uint32_t index,
    UserProcess *result
);

/* Consultar PID asociado a una tarea */
int user_process_pid_by_task(
    uint32_t task_index,
    uint32_t *pid
);

#endif
