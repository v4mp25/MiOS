
#include <stdint.h>
#include <stddef.h>

#include "user_process.h"
#include "user_tasks.h"
#include "address_space.h"
#include "paging.h"
#include "irq.h"

static UserProcess processes[MIOS_USER_TASK_COUNT];

/*
 * PID incremental.
 *
 * No reiniciar este contador al
 * liberar los registros.
 */
static uint32_t next_pid = 1;

static void process_clear(uint32_t i) {
    processes[i].pid = 0;
    processes[i].task_index = 0;
    processes[i].address_space_id = 0;
    processes[i].cr3 = 0;
    processes[i].used = 0;
}

uint32_t user_process_count(void) {
    uint32_t flags = irq_save();
    uint32_t count = 0;

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT; i++) {
        if (processes[i].used)
            count++;
    }

    irq_restore(flags);
    return count;
}

int user_process_prepare(void) {
    if (!paging_cpu_enabled() ||
        as_count() != 0 ||
        user_process_count() != 0) {
        return -1;
    }

    /*
     * Cada proceso utiliza una de las
     * tareas USER existentes.
     */
    if (user_tasks_prepare() != 0)
        return -1;

    if (user_tasks_verify() != 0 ||
        user_tasks_context_verify() != 0) {
        user_tasks_cleanup();
        return -1;
    }

    /*
     * Evitar que el contador de PID
     * desborde al asignar dos procesos.
     */
    if (next_pid == 0 ||
        next_pid > UINT32_MAX -
                   MIOS_USER_TASK_COUNT) {
        user_tasks_cleanup();
        return -1;
    }

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT; i++) {

        const UserTask *task = user_task_get(i);

        if (task == NULL ||
            task->address_space_id == 0 ||
            task->cr3 == 0) {
            user_tasks_cleanup();
            return -1;
        }

        processes[i].pid = next_pid++;
        processes[i].task_index = i;
        processes[i].address_space_id =
            task->address_space_id;
        processes[i].cr3 = task->cr3;
        processes[i].used = 1;
    }

    return 0;
}

int user_process_find(
    uint32_t pid,
    UserProcess *result
) {
    if (pid == 0 || result == NULL)
        return -1;

    uint32_t flags = irq_save();

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT; i++) {

        if (processes[i].used &&
            processes[i].pid == pid) {

            *result = processes[i];
            irq_restore(flags);
            return 0;
        }
    }

    irq_restore(flags);
    return -1;
}

int user_process_verify(void) {
    if (user_process_count() !=
        MIOS_USER_TASK_COUNT) {
        return -1;
    }

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT; i++) {

        const UserProcess *process =
            &processes[i];

        const UserTask *task =
            user_task_get(i);

        if (!process->used ||
            process->pid == 0 ||
            process->task_index != i ||
            task == NULL ||
            task->state != USER_TASK_READY) {
            return -1;
        }

        if (process->cr3 != task->cr3 ||
            process->address_space_id !=
                task->address_space_id) {
            return -1;
        }

        UserProcess found;

        if (user_process_find(
                process->pid,
                &found
            ) != 0 ||
            found.task_index != i) {
            return -1;
        }
    }

    /* PID y CR3 no deben repetirse */
    if (processes[0].pid ==
        processes[1].pid) {
        return -1;
    }

    if (processes[0].cr3 ==
        processes[1].cr3) {
        return -1;
    }

    return 0;
}

int user_process_cleanup(void) {
    /*
     * Liberar primero los espacios
     * propietarios de los recursos.
     */
    if (user_tasks_cleanup() != 0)
        return -1;

    /*
     * Solo borrar los registros cuando
     * las tareas ya fueron liberadas.
     */
    uint32_t flags = irq_save();

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT; i++) {
        process_clear(i);
    }

    irq_restore(flags);
    return 0;
}
