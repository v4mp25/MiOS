
#include <stdint.h>
#include "syscall.h"
#include "irq.h"
#include "user_tasks.h"
#include "user_scheduler.h"
#include "user_process.h"
/*
 * MiOS - Servicios del kernel
 *
 * 1 = TEST
 * 2 = EXIT (solicitud provisional)
 */

static volatile uint32_t test_count = 0;
static volatile uint32_t exit_count = 0;

/*
 * Auditoria de syscalls de la sesion USER.
 * Un registro por cada tarea del prototipo.
 */
static SyscallAudit syscall_audit[MIOS_USER_TASK_COUNT];

void syscall_audit_reset(void) {
    uint32_t flags = irq_save();

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {
        syscall_audit[i].pid = 0;
        syscall_audit[i].test_calls = 0;
        syscall_audit[i].exit_calls = 0;
    }

    irq_restore(flags);
}

int syscall_audit_get(
    uint32_t task_index,
    SyscallAudit *result
) {
    if (task_index >= MIOS_USER_TASK_COUNT ||
        result == 0) {
        return -1;
    }

    uint32_t flags = irq_save();

    *result = syscall_audit[task_index];

    irq_restore(flags);
    return 0;
}



void syscall_dispatch(uint32_t number) {
    uint32_t flags = irq_save();

    /*
     * Identificar el proceso que solicito
     * el servicio, antes de que SYS_EXIT
     * cambie a la siguiente tarea.
     */
    if (user_runtime_active()) {
        int task_index = user_rr_current();
        uint32_t pid = 0;

        if (task_index >= 0 &&
            task_index < (int)MIOS_USER_TASK_COUNT &&
            user_process_current_pid(&pid) == 0) {

            SyscallAudit *audit =
                &syscall_audit[task_index];

            audit->pid = pid;

            if (number == MIOS_SYS_TEST) {
                audit->test_calls++;
            } else if (number == MIOS_SYS_EXIT) {
                audit->exit_calls++;
            }
        }
    }

    switch (number) {
        case MIOS_SYS_TEST:
            test_count++;
            break;

        case MIOS_SYS_EXIT:
            /*
             * El ensamblador se encarga
             * de finalizar la tarea.
             */
            exit_count++;
            break;

        default:
            break;
    }

    irq_restore(flags);
}


uint32_t syscall_test_count(void) {
    uint32_t flags = irq_save();
    uint32_t result = test_count;
    irq_restore(flags);
    return result;
}

uint32_t syscall_exit_count(void) {
    uint32_t flags = irq_save();
    uint32_t result = exit_count;
    irq_restore(flags);
    return result;
}

/*
 * Contadores por nivel de privilegio
 * desde donde se invoco int 0x80.
 */
static volatile uint32_t ring3_calls = 0;
static volatile uint32_t ring0_calls = 0;

void syscall_record_origin(uint32_t saved_cs) {
    uint32_t cpl = saved_cs & 3U;

    if (cpl == 3U) {
        ring3_calls++;
    } else if (cpl == 0U) {
        ring0_calls++;
    }
}

uint32_t syscall_ring3_count(void) {
    uint32_t flags = irq_save();
    uint32_t result = ring3_calls;
    irq_restore(flags);
    return result;
}

uint32_t syscall_ring0_count(void) {
    uint32_t flags = irq_save();
    uint32_t result = ring0_calls;
    irq_restore(flags);
    return result;
}
