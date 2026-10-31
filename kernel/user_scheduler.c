
#include <stdint.h>

#include "user_scheduler.h"
#include "user_tasks.h"
#include "irq.h"

/*
 * Estado independiente del planificador
 * del kernel que ya utiliza MiOS.
 */
static int rr_current = -1;
static uint32_t rr_switch_count = 0;

void user_rr_reset(void) {
    uint32_t flags = irq_save();

    rr_current = -1;
    rr_switch_count = 0;

    irq_restore(flags);
}

int user_rr_current(void) {
    uint32_t flags = irq_save();
    int result = rr_current;
    irq_restore(flags);
    return result;
}

uint32_t user_rr_switches(void) {
    uint32_t flags = irq_save();
    uint32_t result = rr_switch_count;
    irq_restore(flags);
    return result;
}

/*
 * Buscar la siguiente tarea READY.
 * En esta etapa hay dos tareas.
 */
int user_rr_next(void) {
    uint32_t flags = irq_save();

    int previous = rr_current;

    for (uint32_t step = 1;
         step <= MIOS_USER_TASK_COUNT;
         step++) {

        uint32_t index =
            (uint32_t)(previous + (int)step) %
            MIOS_USER_TASK_COUNT;

        const UserTask *task =
            user_task_get(index);

        if (task != 0 &&
            task->state == USER_TASK_READY) {

            rr_current = (int)index;

            if (previous >= 0 &&
                previous != rr_current) {
                rr_switch_count++;
            }

            irq_restore(flags);
            return rr_current;
        }
    }

    rr_current = -1;

    irq_restore(flags);
    return -1;
}

/*
 * Comprueba la alternancia:
 * tarea 0, tarea 1, tarea 0, tarea 1...
 *
 * No ejecuta codigo Ring 3.
 */
int user_rr_selftest(void) {
    static const int expected[6] = {
        0, 1, 0, 1, 0, 1
    };

    user_rr_reset();

    for (uint32_t i = 0; i < 6; i++) {
        int selected = user_rr_next();

        if (selected != expected[i]) {
            user_rr_reset();
            return -1;
        }
    }

    if (user_rr_switches() != 5U ||
        user_rr_current() != 1) {
        user_rr_reset();
        return -1;
    }

    user_rr_reset();
    return 0;
}
