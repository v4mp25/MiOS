
#include <stdint.h>
#include <stddef.h>

#include "process.h"
#include "thread.h"
#include "address_space.h"
#include "paging.h"

typedef struct {
    ProcessInfo info;
    void (*entry)(void);
} Process;

static Process processes[MIOS_MAX_PROCESSES];
static uint32_t next_pid = 1;

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;

    while (src[i] &&
           i < PROCESS_NAME_SIZE - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void process_entry_0(void) {
    processes[0].info.state = PROCESS_RUNNING;

    processes[0].entry();

    processes[0].info.state = PROCESS_TERMINATED;
}

static void process_entry_1(void) {
    processes[1].info.state = PROCESS_RUNNING;

    processes[1].entry();

    processes[1].info.state = PROCESS_TERMINATED;
}

/* Inicializar tabla de procesos */
void process_init(void) {
    /*
     * No reinicializar si quedan espacios
     * asociados a procesos anteriores.
     */
    for (uint32_t i = 0; i < MIOS_MAX_PROCESSES; i++) {
        if (processes[i].info.asid != 0)
            return;
    }

    next_pid = 1;

    for (uint32_t i = 0; i < MIOS_MAX_PROCESSES; i++) {
        processes[i].info.pid = 0;
        processes[i].info.name[0] = '\0';
        processes[i].info.state = PROCESS_FREE;
        processes[i].info.asid = 0;
        processes[i].entry = NULL;
    }
}

/* Crear tarea con espacio de direcciones propio */
int process_create(
    const char *name,
    void (*function)(void)
) {
    if (name == NULL || function == NULL ||
        !paging_cpu_enabled())
        return -1;

    for (uint32_t i = 0; i < MIOS_MAX_PROCESSES; i++) {
        if (processes[i].info.state != PROCESS_FREE)
            continue;

        int asid = as_create();

        if (asid < 0)
            return -1;

        void (*wrapper)(void) =
            (i == 0) ? process_entry_0 : process_entry_1;

        int tid = thread_create(wrapper);

        if (tid < 0) {
            as_destroy((uint32_t)asid);
            return -1;
        }

        processes[i].info.pid = next_pid++;
        processes[i].info.asid = (uint32_t)asid;

        copy_name(processes[i].info.name, name);

        processes[i].entry = function;
        processes[i].info.state = PROCESS_READY;

        return (int)processes[i].info.pid;
    }

    return -1;
}

/*
 * Ejecuta las tareas usando el planificador
 * actual. Todavia NO cambia CR3 por proceso.
 */
int process_run(void) {
    int result = thread_run();

    if (result != 0)
        return -1;

    /* Recuperar los espacios al terminar */
    for (uint32_t i = 0; i < MIOS_MAX_PROCESSES; i++) {
        if (processes[i].info.state == PROCESS_TERMINATED &&
            processes[i].info.asid != 0) {

            if (as_destroy(processes[i].info.asid) != 0)
                return -1;

            processes[i].info.asid = 0;
        }
    }

    return 0;
}

int process_get_info(
    uint32_t index,
    ProcessInfo *out
) {
    if (index >= MIOS_MAX_PROCESSES || out == NULL)
        return -1;

    *out = processes[index].info;
    return 0;
}
