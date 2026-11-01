
#include <stdint.h>
#include <stddef.h>

#include "elf_exec.h"
#include "elf32.h"
#include "binfs.h"
#include "address_space.h"
#include "paging.h"
#include "ring3.h"
#include "syscall.h"
#include "tss.h"
#include "irq.h"

/*
 * Ejecutor experimental MiOS.
 *
 * Solo ejecuta un ELF de confianza
 * por vez. No admite ejecucion anidada.
 */

static uint8_t elf_exec_buffer[BINFS_MAX_FILE_SIZE];

static uint8_t elf_exec_kernel_stack[16384]
    __attribute__((aligned(16)));

static int elf_exec_busy = 0;

int elf_exec_image(
    const void *image,
    uint32_t size
) {
    if (image == NULL || size == 0U)
        return -1;

    if (!paging_cpu_enabled())
        return -1;

    if (as_count() != 0U)
        return -1;

    if (elf_exec_busy)
        return -1;

    Elf32_LoadInfo info;

    if (elf32_validate_segments(
            image, size, &info
        ) != 0)
        return -1;

    elf_exec_busy = 1;

    int id = as_create();

    if (id < 0) {
        elf_exec_busy = 0;
        return -1;
    }

    int ok = 1;

    uint32_t user_cr3 = 0;
    uint32_t code_frame = 0;
    uint32_t stack_frame = 0;
    uint32_t entry = 0;

    if (as_get(
            (uint32_t)id,
            &user_cr3,
            &code_frame
        ) != 0 ||
        as_get_user_stack(
            (uint32_t)id,
            &stack_frame
        ) != 0) {
        ok = 0;
    }

    if (ok && elf32_load(
            image,
            size,
            (uint32_t)id,
            &entry
        ) != 0) {
        ok = 0;
    }

    if (ok && entry != info.entry)
        ok = 0;

    if (ok) {
        uint32_t test_before =
            syscall_test_count();

        uint32_t exit_before =
            syscall_exit_count();

        uint32_t old_esp0 =
            tss_get_kernel_stack();

        uint32_t flags = irq_save();

        uint32_t kernel_top =
            (uint32_t)(uintptr_t)(
                elf_exec_kernel_stack +
                sizeof(elf_exec_kernel_stack)
            );

        tss_set_kernel_stack(kernel_top);

        /*
         * Ejecutar desde el punto de
         * entrada indicado por el ELF.
         */
        ring3_enter_at(
            user_cr3,
            MIOS_AS_USER_STACK_TOP,
            entry
        );

        /* Retorno normal por SYS_EXIT */
        tss_set_kernel_stack(old_esp0);
        irq_restore(flags);

        if (syscall_test_count() !=
            test_before + 1U)
            ok = 0;

        if (syscall_exit_count() !=
            exit_before + 1U)
            ok = 0;
    }

    /*
     * Nunca destruir un espacio si
     * la CPU todavia utiliza su CR3.
     */
    uint32_t current_cr3 = 0;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(current_cr3)
        :
        : "memory"
    );

    current_cr3 &= 0xFFFFF000U;

    if (current_cr3 !=
        paging_directory_address()) {
        /*
         * Estado critico: no liberar memoria
         * ni permitir otra ejecucion.
         */
        elf_exec_busy = 1;
        return -1;
    }

    if (as_destroy((uint32_t)id) != 0) {
        elf_exec_busy = 1;
        return -1;
    }

    elf_exec_busy = 0;

    return ok ? 0 : -1;
}

int elf_exec_file(const char *filename) {
    if (filename == NULL || !filename[0])
        return -1;

    if (!paging_cpu_enabled() ||
        as_count() != 0U ||
        elf_exec_busy)
        return -1;

    uint32_t size = 0;

    if (binfs_read(
            filename,
            elf_exec_buffer,
            sizeof(elf_exec_buffer),
            &size
        ) != 0) {
        return -1;
    }

    return elf_exec_image(
        elf_exec_buffer,
        size
    );
}
