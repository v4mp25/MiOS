
#include <stdint.h>
#include "syscall.h"
#include "irq.h"

/*
 * MiOS - Servicios del kernel
 *
 * 1 = TEST
 * 2 = EXIT (solicitud provisional)
 */

static volatile uint32_t test_count = 0;
static volatile uint32_t exit_count = 0;

void syscall_dispatch(uint32_t number) {
    uint32_t flags = irq_save();

    switch (number) {
        case MIOS_SYS_TEST:
            test_count++;
            break;

        case MIOS_SYS_EXIT:
            /*
             * Por ahora registrar la solicitud.
             * Todavia NO finalizar el proceso.
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
