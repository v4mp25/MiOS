
#ifndef MIOS_ADDRESS_SPACE_H
#define MIOS_ADDRESS_SPACE_H

#include <stdint.h>

#define MIOS_AS_MAX 2
#define MIOS_AS_PRIVATE_VADDR 0x10800000U

/* Inicializar el administrador */
void as_init(void);

/* Crear un espacio y devolver su ID */
int as_create(void);

/* Destruir un espacio que no este activo */
int as_destroy(uint32_t id);

/* Consultar un espacio */
int as_get(
    uint32_t id,
    uint32_t *cr3,
    uint32_t *private_frame
);

/* Cantidad de espacios existentes */
uint32_t as_count(void);
/* Registrar y retirar usuarios de un espacio */
int as_acquire(uint32_t id);
int as_release(uint32_t id);

/* Consultar cuantos usuarios tiene */
int as_users(uint32_t id);

/* Pila virtual de usuario */
#define MIOS_AS_USER_STACK_BASE 0x10801000U
#define MIOS_AS_USER_STACK_TOP  0x10802000U

/* Consultar el marco de la pila de usuario */
int as_get_user_stack(
    uint32_t id,
    uint32_t *physical_frame
);

#endif
