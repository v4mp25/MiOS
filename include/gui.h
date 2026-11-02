
#ifndef MIOS_GUI_H
#define MIOS_GUI_H

#include <stdint.h>

/* Abrir el escritorio */
void gui_open(void);

/* Cerrar el escritorio */
void gui_close(void);

/* Consultar si esta abierto */
int gui_active(void);

/* Procesar teclas dentro del escritorio */
void gui_key(uint8_t scancode);

#endif
