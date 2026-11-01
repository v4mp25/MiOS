
#include <stdint.h>

#define SYS_TEST  1U
#define SYS_EXIT  2U
#define SYS_WRITE 3U

static void mios_call(uint32_t number) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(number)
        : "memory", "cc"
    );
}

static void mios_write(
    const char *text,
    uint32_t length
) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_WRITE),
          "c"(text),
          "d"(length)
        : "memory", "cc"
    );
}

__attribute__((noreturn))
void _start(void) {
    static const char message[] =
        "CALCULO: programa C ejecutado en MiOS!\n";

    volatile uint32_t total = 0;

    mios_call(SYS_TEST);

    for (uint32_t i = 1; i <= 1000U; i++) {
        total += i;
    }

    if (total == 500500U) {
        mios_write(message, sizeof(message) - 1U);
    }

    mios_call(SYS_EXIT);

    __asm__ volatile ("ud2");
    __builtin_unreachable();
}
