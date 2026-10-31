
#ifndef MIOS_USER_TASKS_H
#define MIOS_USER_TASKS_H

#include <stdint.h>

#define MIOS_USER_TASK_COUNT 2U

#define USER_TASK_FREE  0U
#define USER_TASK_READY 1U

/*
 * Descriptor de una tarea de usuario.
 * Cada tarea administra su propio CR3,
 * pila de usuario y pila privilegiada.
 */
typedef struct {
    uint32_t state;
    uint32_t address_space_id;

    uint32_t cr3;
    uint32_t code_frame;
    uint32_t stack_frame;

    uint32_t user_eip;
    uint32_t user_esp;
    uint32_t kernel_esp0;
} UserTask;

int user_tasks_prepare(void);
int user_tasks_verify(void);
int user_tasks_cleanup(void);

const UserTask *user_task_get(uint32_t index);

#endif
