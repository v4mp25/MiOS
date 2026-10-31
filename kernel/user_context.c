
#include <stdint.h>

#include "user_context.h"
#include "irq.h"

static UserContext last_context;
static volatile uint32_t capture_count = 0;

void user_context_reset(void) {
    uint32_t flags = irq_save();

    uint32_t *p = (uint32_t *)&last_context;

    for (uint32_t i = 0;
         i < sizeof(UserContext) / sizeof(uint32_t);
         i++) {
        p[i] = 0;
    }

    capture_count = 0;

    irq_restore(flags);
}

void user_context_capture(const uint32_t *frame) {
    if (frame == 0)
        return;

    /*
     * Marco creado por irq0_stub:
     *
     * 0-7   registros PUSHAD
     * 8-11  GS, FS, ES, DS
     * 12    EIP
     * 13    CS
     * 14    EFLAGS
     * 15    ESP de usuario
     * 16    SS de usuario
     */

    /* Solo registrar interrupciones de Ring 3 */
    if ((frame[13] & 3U) != 3U)
        return;

    last_context.edi = frame[0];
    last_context.esi = frame[1];
    last_context.ebp = frame[2];

    last_context.ebx = frame[4];
    last_context.edx = frame[5];
    last_context.ecx = frame[6];
    last_context.eax = frame[7];

    last_context.gs = frame[8];
    last_context.fs = frame[9];
    last_context.es = frame[10];
    last_context.ds = frame[11];

    last_context.eip = frame[12];
    last_context.cs = frame[13];
    last_context.eflags = frame[14];

    last_context.user_esp = frame[15];
    last_context.user_ss = frame[16];

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(last_context.cr3)
        :
        : "memory"
    );

    capture_count++;
}

uint32_t user_context_count(void) {
    uint32_t flags = irq_save();

    uint32_t result = capture_count;

    irq_restore(flags);
    return result;
}

int user_context_verify(uint32_t expected_cr3) {
    uint32_t flags = irq_save();

    int ok = 1;

    if (capture_count == 0)
        ok = 0;

    if (last_context.cs != 0x1BU)
        ok = 0;

    if (last_context.user_ss != 0x23U)
        ok = 0;

    if ((last_context.eflags & 0x200U) == 0)
        ok = 0;

    if (last_context.eip < 0x10800000U ||
        last_context.eip >= 0x10801000U)
        ok = 0;

    if (last_context.user_esp <= 0x10801000U ||
        last_context.user_esp > 0x10802000U)
        ok = 0;

    if ((last_context.cr3 & 0xFFFFF000U) !=
        (expected_cr3 & 0xFFFFF000U))
        ok = 0;

    irq_restore(flags);
    return ok;
}
