
#ifndef MIOS_USER_PROGRAM_H
#define MIOS_USER_PROGRAM_H

#include <stdint.h>

/* Direccion virtual del codigo de usuario */
#define MIOS_USER_CODE_VADDR 0x10800000U

/* Preparar un programa en un marco fisico */
int user_program_prepare(uint32_t physical_frame);

/* Comprobar el contenido preparado */
int user_program_verify(uint32_t physical_frame);

#endif
