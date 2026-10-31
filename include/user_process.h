
#ifndef MIOS_USER_PROCESS_H
#define MIOS_USER_PROCESS_H

#include <stdint.h>
#include "user_tasks.h"

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

#endif
