
#ifndef MIOS_MOUSE_H
#define MIOS_MOUSE_H

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;

    uint8_t left;
    uint8_t right;
    uint8_t middle;

    uint8_t available;
    uint32_t packet_count;
} MouseState;

/* Inicializa el raton PS/2 */
int mouse_init(void);

/* Procesa un byte del raton */
void mouse_handle_byte(uint8_t data);

/* Devuelve el estado mas reciente */
void mouse_get_state(MouseState *state);

#endif
