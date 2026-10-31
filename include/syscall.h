
#ifndef MIOS_SYSCALL_H
#define MIOS_SYSCALL_H

#include <stdint.h>

#define MIOS_SYS_TEST 1U
#define MIOS_SYS_EXIT 2U
/* Recibe solicitudes desde el ASM */
void syscall_dispatch(uint32_t number);

/* Consultar cantidad de llamadas */
uint32_t syscall_test_count(void);
uint32_t syscall_exit_count(void);

/* Registrar el nivel de privilegio de origen */
void syscall_record_origin(uint32_t saved_cs);

/* Numero de syscalls originadas en Ring 3 */
uint32_t syscall_ring3_count(void);

/* Numero de syscalls originadas en Ring 0 */
uint32_t syscall_ring0_count(void);

/*
 * Registro de servicios por tarea.
 * El PID identifica el proceso real.
 */
typedef struct {
    uint32_t pid;
    uint32_t test_calls;
    uint32_t exit_calls;
} SyscallAudit;

/* Reiniciar el registro de la prueba */
void syscall_audit_reset(void);

/* Consultar servicios de una tarea */
int syscall_audit_get(
    uint32_t task_index,
    SyscallAudit *result
);

#endif

