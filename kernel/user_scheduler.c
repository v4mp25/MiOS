
#include <stdint.h>

#include "user_scheduler.h"
#include "user_tasks.h"
#include "irq.h"
#include "tss.h"
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

/*
 * Ejecucion de tareas Ring 3.
 *
 * Esta primera version admite una unica
 * sesion de dos tareas a la vez.
 */

static int runtime_active = 0;
static int runtime_error = 0;
static uint32_t runtime_irq_switches = 0;
static uint32_t runtime_task_exits[MIOS_USER_TASK_COUNT];
static uint32_t runtime_total_exits = 0;
static void runtime_load_address_space(
    uint32_t cr3,
    uint32_t esp0
) {
    /*
     * Actualizar la pila Ring 0 para
     * futuras interrupciones Ring 3.
     */
    tss_set_kernel_stack(esp0);

    uint32_t current_cr3;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(current_cr3)
        :
        : "memory"
    );

    if ((current_cr3 & 0xFFFFF000U) !=
        (cr3 & 0xFFFFF000U)) {

        __asm__ volatile (
            "mov %0, %%cr3"
            :
            : "r"(cr3)
            : "memory"
        );
    }
}

int user_runtime_start(void) {
    uint32_t flags = irq_save();

    if (runtime_active ||
        user_tasks_verify() != 0 ||
        user_tasks_context_verify() != 0) {

        irq_restore(flags);
        return -1;
    }

    user_rr_reset();

    runtime_error = 0;
    runtime_irq_switches = 0;
    runtime_total_exits = 0;

for (uint32_t i = 0; i < MIOS_USER_TASK_COUNT; i++) {
    runtime_task_exits[i] = 0;
}

    int first = user_rr_next();

    if (first != 0) {
        irq_restore(flags);
        return -1;
    }

    runtime_active = 1;

    irq_restore(flags);
    return 0;
}

int user_runtime_active(void) {
    return runtime_active;
}

uint32_t user_runtime_irq_switches(void) {
    return runtime_irq_switches;
}

int user_runtime_error(void) {
    return runtime_error;
}

/*
 * Se llama desde IRQ0 cuando la CPU
 * interrumpio un programa Ring 3.
 */
uint32_t user_runtime_tick(uint32_t saved_esp) {
    if (!runtime_active)
        return saved_esp;

    int previous = user_rr_current();

    if (previous < 0) {
        runtime_error = 1;
        return saved_esp;
    }

    if (user_task_save_context(
            (uint32_t)previous,
            saved_esp
        ) != 0) {

        runtime_error = 1;
        return saved_esp;
    }

    int next = user_rr_next();

    if (next < 0) {
        runtime_error = 1;
        return saved_esp;
    }

    if (next == previous)
        return saved_esp;

    uint32_t next_esp = 0;
    uint32_t next_cr3 = 0;
    uint32_t next_esp0 = 0;

    if (user_task_get_context(
            (uint32_t)next,
            &next_esp,
            &next_cr3,
            &next_esp0
        ) != 0) {

        runtime_error = 1;
        return saved_esp;
    }

    /*
     * El codigo y las pilas privilegiadas
     * estan en el mapeo comun del kernel.
     */
    runtime_load_address_space(
        next_cr3,
        next_esp0
    );

    runtime_irq_switches++;

    return next_esp;
}

/*
 * Se llama desde SYS_EXIT de una tarea
 * Ring 3. No debe volver a la tarea
 * que acaba de terminar.
 */
uint32_t user_runtime_exit(void) {
    if (!runtime_active)
        return 0;

    int previous = user_rr_current();

    if (previous < 0 ||
        user_task_mark_done(
            (uint32_t)previous
        ) != 0) {

        runtime_error = 1;
        runtime_active = 0;
        return 0;
    }
/* La tarea actual terminó correctamente */
runtime_task_exits[previous]++;
runtime_total_exits++;
    int next = user_rr_next();

    if (next < 0) {
        /* Ambas tareas terminaron */
        runtime_active = 0;
        return 0;
    }

    uint32_t next_esp = 0;
    uint32_t next_cr3 = 0;
    uint32_t next_esp0 = 0;

    if (user_task_get_context(
            (uint32_t)next,
            &next_esp,
            &next_cr3,
            &next_esp0
        ) != 0) {

        runtime_error = 1;
        runtime_active = 0;
        return 0;
    }

    runtime_load_address_space(
        next_cr3,
        next_esp0
    );

    return next_esp;
}


uint32_t user_runtime_task_exits(uint32_t index) {
    if (index >= MIOS_USER_TASK_COUNT)
        return 0;

    uint32_t flags = irq_save();

    uint32_t result = runtime_task_exits[index];

    irq_restore(flags);
    return result;
}

uint32_t user_runtime_total_exits(void) {
    uint32_t flags = irq_save();

    uint32_t result = runtime_total_exits;

    irq_restore(flags);
    return result;
}
