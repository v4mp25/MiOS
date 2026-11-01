
/*
 * MiOS - Primer programa C con SYS_WRITE
 *
 * Se ejecuta en Ring 3.
 * No necesita Linux ni libc.
 */

#include <stdint.h>

#define SYS_TEST  1U
#define SYS_EXIT  2U
#define SYS_WRITE 3U

static void mios_syscall(uint32_t number) {
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
        "Hola desde un programa C en Ring 3!\n";

    /* Probar la comunicacion con el kernel */
    mios_syscall(SYS_TEST);

    /* Imprimir desde el propio programa */
    mios_write(
        message,
        sizeof(message) - 1U
    );

    /* Terminar el programa */
    mios_syscall(SYS_EXIT);

    /* SYS_EXIT no debe regresar */
    __asm__ volatile ("ud2");

    __builtin_unreachable();
}
