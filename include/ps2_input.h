
#ifndef MIOS_PS2_INPUT_H
#define MIOS_PS2_INPUT_H

#include <stdint.h>

#define PS2_INPUT_NONE     0
#define PS2_INPUT_KEYBOARD 1
#define PS2_INPUT_MOUSE    2

/*
 * Leer un byte PS/2 si esta disponible.
 *
 * Devuelve el dispositivo de origen.
 *
 * 0 = sin datos
 * 1 = teclado
 * 2 = raton
 *
 * No habilita interrupciones ni
 * modifica el controlador.
 */
int ps2_input_read(uint8_t *data);

#endif
