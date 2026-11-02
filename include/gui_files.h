
#ifndef MIOS_GUI_FILES_H
#define MIOS_GUI_FILES_H

#include <stdint.h>

/* Preparar el explorador y cargar archivos */
void gui_files_open(void);

/* Dibujar archivos en la ventana VGA */
void gui_files_draw(void);

/*
 * Procesar una tecla.
 * Devuelve 1 si pertenece al explorador.
 */
int gui_files_key(uint8_t scancode);

#endif
