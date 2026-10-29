
#include <stdint.h>
#include <stddef.h>

#include "process.h"
#include "thread.h"

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

/* Punto de entrada del primer proceso */
static void process_entry_0(void) {
    processes[0].info.state = PROCESS_RUNNING;

    processes[0].entry();

    processes[0].info.state = PROCESS_TERMINATED;
}

/* Punto de entrada del segundo proceso */
static void process_entry_1(void) {
    processes[1].info.state = PROCESS_RUNNING;

    processes[1].entry();

    processes[1].info.state = PROCESS_TERMINATED;
}

void process_init(void) {
    next_pid = 1;

    for (uint32_t i = 0;
         i < MIOS_MAX_PROCESSES;
         i++) {

        processes[i].info.pid = 0;
        processes[i].info.name[0] = '\0';
        processes[i].info.state = PROCESS_FREE;
        processes[i].entry = NULL;
    }
}

int process_create(
    const char *name,
    void (*function)(void)
) {
    if (name == NULL || function == NULL)
        return -1;

    for (uint32_t i = 0;
         i < MIOS_MAX_PROCESSES;
         i++) {

        if (processes[i].info.state
            != PROCESS_FREE)
            continue;

        void (*wrapper)(void);

        if (i == 0)
            wrapper = process_entry_0;
        else
            wrapper = process_entry_1;

        int tid = thread_create(wrapper);

        if (tid < 0)
            return -1;

        processes[i].info.pid = next_pid++;
        copy_name(processes[i].info.name, name);

        processes[i].entry = function;
        processes[i].info.state = PROCESS_READY;

        return (int)processes[i].info.pid;
    }

    return -1;
}

int process_run(void) {
    return thread_run();
}

int process_get_info(
    uint32_t index,
    ProcessInfo *out
) {
    if (index >= MIOS_MAX_PROCESSES ||
        out == NULL)
        return -1;

    *out = processes[index].info;

    return 0;
}
