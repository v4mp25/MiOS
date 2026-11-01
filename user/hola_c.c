
/*
 * Primer programa de usuario
 * escrito en C para MiOS.
 *
 * No utiliza Linux ni libc.
 */

#include <stdint.h>

#define SYS_TEST 1U
#define SYS_EXIT 2U

static void syscall_mios(uint32_t numero) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(numero)
        : "memory", "cc"
    );
}

__attribute__((noreturn))
void _start(void) {

    /* Primera llamada al kernel */
    syscall_mios(SYS_TEST);

    /*
     * Trabajo sencillo en Ring 3.
     * volatile evita eliminar el bucle.
     */
    volatile uint32_t contador = 0;

    for (uint32_t i = 0; i < 1000000U; i++) {
        contador++;
    }

    /* Solicitar la finalizacion */
    syscall_mios(SYS_EXIT);

    /*
     * SYS_EXIT no deberia regresar
     * en nuestro ejecutor actual.
     */
    __asm__ volatile ("ud2");

    __builtin_unreachable();
}
