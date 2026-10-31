
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

#endif
