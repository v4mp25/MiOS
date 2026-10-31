
#include <stdint.h>
#include "user_program.h"
#include "pmm.h"

/*
 * Primer programa de usuario MiOS.
 *
 * mov eax, 1
 * int 0x80
 * jmp $
 *
 * Por ahora solamente se prepara.
 * NO se ejecuta todavia.
 */

static const uint8_t program_code[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};

int user_program_prepare(uint32_t physical_frame) {
    if (!pmm_frame_allocated(physical_frame))
        return -1;

    /*
     * En MiOS, los primeros 128 MiB
     * tienen mapeo de identidad.
     */
    uint8_t *memory =
        (uint8_t *)(uintptr_t)physical_frame;

    for (uint32_t i = 0; i < 4096; i++)
        memory[i] = 0;

    for (uint32_t i = 0;
         i < sizeof(program_code);
         i++) {
        memory[i] = program_code[i];
    }

    return 0;
}

int user_program_verify(uint32_t physical_frame) {
    if (!pmm_frame_allocated(physical_frame))
        return -1;

    const uint8_t *memory =
        (const uint8_t *)(uintptr_t)physical_frame;

    for (uint32_t i = 0;
         i < sizeof(program_code);
         i++) {

        if (memory[i] != program_code[i])
            return -1;
    }

    return 0;
}
