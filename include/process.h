
#ifndef MIOS_PROCESS_H
#define MIOS_PROCESS_H

#include <stdint.h>

#define MIOS_MAX_PROCESSES 2
#define PROCESS_NAME_SIZE 20

typedef enum {
    PROCESS_FREE = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_TERMINATED
} ProcessState;


typedef struct {
    uint32_t pid;
    char name[PROCESS_NAME_SIZE];
    ProcessState state;

    /* Espacio de direcciones del proceso */
    uint32_t asid;
} ProcessInfo;

void process_init(void);

int process_create(
    const char *name,
    void (*function)(void)
);

int process_run(void);

int process_get_info(
    uint32_t index,
    ProcessInfo *out
);
/* Recuperar descriptores de procesos terminados */
int process_reap(void);
#endif
