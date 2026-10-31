
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
