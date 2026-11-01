
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
    const void *pointer,
    uint32_t length
) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_WRITE),
          "c"(pointer),
          "d"(length)
        : "memory", "cc"
    );
}

__attribute__((noreturn))
void _start(void) {
    static const char message[] =
        "SYS_WRITE: texto valido desde Ring 3!\n";

    mios_call(SYS_TEST);

    /* 1. Lectura valida */
    mios_write(
        message,
        sizeof(message) - 1U
    );

    /* 2. Intento de leer memoria VGA */
    mios_write(
        (const void *)(uintptr_t)0x000B8000U,
        10U
    );

    /* 3. Intento de cruzar el limite USER */
    mios_write(
        (const void *)(uintptr_t)0x10801FFFU,
        4U
    );

    /* 4. Direccion fuera de rango */
    mios_write(
        (const void *)(uintptr_t)0xFFFFFFFFU,
        4U
    );

    /* 5. Longitud superior al maximo */
    mios_write(
        message,
        161U
    );

    mios_call(SYS_EXIT);

    /* SYS_EXIT no debe regresar */
    __asm__ volatile ("ud2");
    __builtin_unreachable();
}
