
#ifndef MIOS_GUI_H
#define MIOS_GUI_H

#include <stdint.h>

/* Abrir y cerrar el escritorio */
void gui_open(void);
void gui_close(void);

/* Consultar estado */
int gui_active(void);

/* Procesar una tecla del escritorio */
void gui_key(uint8_t scancode);

/*
 * Actualizar monitores dinamicos.
 * Llamar desde el bucle principal,
 * NO desde una interrupcion.
 */
void gui_update(void);

#endif
