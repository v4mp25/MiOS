
#ifndef MIOS_USER_COPY_H
#define MIOS_USER_COPY_H

#include <stdint.h>

/*
 * Comprueba que una region pertenece
 * a las paginas USER del prototipo MiOS.
 *
 * 0 = valida
 * -1 = invalida
 */
int user_memory_readable(
    uint32_t address,
    uint32_t length
);

#endif
