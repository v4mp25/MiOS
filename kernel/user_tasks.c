
#include <stdint.h>
#include <stddef.h>

#include "user_tasks.h"
#include "address_space.h"
#include "user_program.h"
#include "paging.h"

/*
 * Dos pilas privilegiadas independientes.
 *
 * No son pilas de usuario: la CPU las
 * utilizara durante transiciones hacia
 * Ring 0, mediante la TSS.
 */
static uint8_t kernel_stacks
    [MIOS_USER_TASK_COUNT][16384]
    __attribute__((aligned(16)));

static UserTask tasks[MIOS_USER_TASK_COUNT];

/*
 * Prepara el contexto inicial de una tarea.
 *
 * El marco tiene el mismo orden que produce
 * irq0_stub al guardar un contexto Ring 3:
 *
 * PUSHAD (8 palabras)
 * GS, FS, ES, DS (4 palabras)
 * EIP, CS, EFLAGS, ESP, SS (5 palabras)
 *
 * Total: 17 palabras de 32 bits.
 */
static void user_task_build_initial_frame(uint32_t i) {
    UserTask *task = &tasks[i];

    uint32_t *frame =
        (uint32_t *)(uintptr_t)
        (task->kernel_esp0 - 17U * sizeof(uint32_t));

    /* Inicializar los registros generales */
    for (uint32_t j = 0; j < 17U; j++) {
        frame[j] = 0;
    }

    /*
     * Registros PUSHAD:
     * 0 EDI
     * 1 ESI
     * 2 EBP
     * 3 ESP ignorado por POPAD
     * 4 EBX
     * 5 EDX
     * 6 ECX
     * 7 EAX
     */

    /* Segmentos de usuario */
    frame[8]  = 0x23U; /* GS */
    frame[9]  = 0x23U; /* FS */
    frame[10] = 0x23U; /* ES */
    frame[11] = 0x23U; /* DS */

    /* Marco IRETD hacia Ring 3 */
    frame[12] = task->user_eip;
    frame[13] = 0x1BU;     /* CS de usuario */
    frame[14] = 0x00000202U; /* EFLAGS, IF=1 */
    frame[15] = task->user_esp;
    frame[16] = 0x23U;     /* SS de usuario */

    /* Guardar punto de restauracion */
    task->saved_kernel_esp =
        (uint32_t)(uintptr_t)frame;
}


static void task_clear(uint32_t i) {
    tasks[i].state = USER_TASK_FREE;
    tasks[i].address_space_id = 0;
    tasks[i].cr3 = 0;
    tasks[i].code_frame = 0;
    tasks[i].stack_frame = 0;
    tasks[i].user_eip = 0;
    tasks[i].user_esp = 0;
    tasks[i].kernel_esp0 = 0;
    tasks[i].saved_kernel_esp = 0;
}

const UserTask *user_task_get(uint32_t index) {
    if (index >= MIOS_USER_TASK_COUNT)
        return NULL;

    return &tasks[index];
}

int user_tasks_cleanup(void) {
    int result = 0;

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {

        if (tasks[i].address_space_id != 0U) {
            if (as_destroy(
                    tasks[i].address_space_id
                ) != 0) {

                result = -1;
                continue;
            }
        }

        task_clear(i);
    }

    return result;
}

int user_tasks_prepare(void) {
    if (!paging_cpu_enabled())
        return -1;

    if (as_count() != 0U)
        return -1;

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {

        if (tasks[i].address_space_id != 0U)
            return -1;
    }

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {

        int id = as_create();

        if (id < 0)
            goto fail;

        tasks[i].address_space_id =
            (uint32_t)id;

        if (as_get(
                (uint32_t)id,
                &tasks[i].cr3,
                &tasks[i].code_frame
            ) != 0)
            goto fail;

        if (as_get_user_stack(
                (uint32_t)id,
                &tasks[i].stack_frame
            ) != 0)
            goto fail;

        if (user_program_prepare(
                tasks[i].code_frame
            ) != 0)
            goto fail;

        if (user_program_verify(
                tasks[i].code_frame
            ) != 0)
            goto fail;

        tasks[i].user_eip =
            MIOS_USER_CODE_VADDR;

        tasks[i].user_esp =
            MIOS_AS_USER_STACK_TOP;

        tasks[i].kernel_esp0 =
            (uint32_t)(uintptr_t)(
                kernel_stacks[i] + 16384
            );
/* Preparar el contexto inicial de CPU */
user_task_build_initial_frame(i);
        tasks[i].state = USER_TASK_READY;
    }

    return 0;

fail:
    /*
     * La limpieza solo destruye espacios
     * que se hayan creado correctamente.
     */
    user_tasks_cleanup();
    return -1;
}

int user_tasks_verify(void) {
    if (!paging_cpu_enabled())
        return -1;

    if (as_count() != MIOS_USER_TASK_COUNT)
        return -1;

    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {

        if (tasks[i].state != USER_TASK_READY)
            return -1;

        if (tasks[i].address_space_id == 0U ||
            tasks[i].cr3 == 0U ||
            tasks[i].code_frame == 0U ||
            tasks[i].stack_frame == 0U)
            return -1;

        if (tasks[i].user_eip !=
            MIOS_USER_CODE_VADDR)
            return -1;

        if (tasks[i].user_esp !=
            MIOS_AS_USER_STACK_TOP)
            return -1;

        if (tasks[i].kernel_esp0 == 0U)
            return -1;

        if (user_program_verify(
                tasks[i].code_frame
            ) != 0)
            return -1;

        /*
         * Verificar paginas USER en las
         * tablas de cada espacio.
         *
         * El kernel utiliza un mapeo de
         * identidad para esos marcos.
         */
        uint32_t *directory =
            (uint32_t *)(uintptr_t)
            tasks[i].cr3;

        uint32_t pde = directory[66];

        if ((pde & 7U) != 7U)
            return -1;

        uint32_t *table =
            (uint32_t *)(uintptr_t)
            (pde & 0xFFFFF000U);

        uint32_t code_pte = table[0];
        uint32_t stack_pte = table[1];

        if ((code_pte & 7U) != 7U ||
            (stack_pte & 7U) != 7U)
            return -1;

        if ((code_pte & 0xFFFFF000U) !=
            tasks[i].code_frame)
            return -1;

        if ((stack_pte & 0xFFFFF000U) !=
            tasks[i].stack_frame)
            return -1;

        /* Mantener el kernel como supervisor */
        if ((directory[0] & 4U) != 0U)
            return -1;
    }

    /*
     * Los recursos fisicos deben ser
     * independientes entre ambas tareas.
     */
    if (tasks[0].cr3 == tasks[1].cr3)
        return -1;

    if (tasks[0].code_frame ==
        tasks[1].code_frame)
        return -1;

    if (tasks[0].stack_frame ==
        tasks[1].stack_frame)
        return -1;

    if (tasks[0].kernel_esp0 ==
        tasks[1].kernel_esp0)
        return -1;

    return 0;
}

int user_tasks_context_verify(void) {
    for (uint32_t i = 0;
         i < MIOS_USER_TASK_COUNT;
         i++) {

        const UserTask *task = &tasks[i];

        if (task->state != USER_TASK_READY)
            return -1;

        uint32_t expected_esp =
            (uint32_t)(uintptr_t)(
                kernel_stacks[i] + 16384
            ) - 17U * sizeof(uint32_t);

        if (task->saved_kernel_esp != expected_esp)
            return -1;

        const uint32_t *frame =
            (const uint32_t *)(uintptr_t)
            task->saved_kernel_esp;

        /* Registros generales inicialmente en cero */
        for (uint32_t j = 0; j < 8U; j++) {
            if (frame[j] != 0U)
                return -1;
        }

        /* Selectores de datos Ring 3 */
        if (frame[8]  != 0x23U ||
            frame[9]  != 0x23U ||
            frame[10] != 0x23U ||
            frame[11] != 0x23U) {
            return -1;
        }

        /* Marco de retorno Ring 3 */
        if (frame[12] != task->user_eip ||
            frame[13] != 0x1BU ||
            frame[14] != 0x00000202U ||
            frame[15] != task->user_esp ||
            frame[16] != 0x23U) {
            return -1;
        }
    }

    /* Ninguna tarea comparte su contexto */
    if (tasks[0].saved_kernel_esp ==
        tasks[1].saved_kernel_esp) {
        return -1;
    }

    return 0;
}
