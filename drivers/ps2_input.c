
#include <stdint.h>

#include "ps2_input.h"

#define PS2_DATA_PORT   0x60U
#define PS2_STATUS_PORT 0x64U

#define PS2_OUTPUT_FULL 0x01U
#define PS2_AUX_DATA    0x20U

static inline uint8_t ps2_inb(
    uint16_t port
) {
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

int ps2_input_read(uint8_t *data) {
    if (data == 0)
        return PS2_INPUT_NONE;

    /*
     * Leer primero el estado,
     * antes de consumir el dato.
     */
    uint8_t status =
        ps2_inb(PS2_STATUS_PORT);

    if ((status & PS2_OUTPUT_FULL) == 0U) {
        return PS2_INPUT_NONE;
    }

    /*
     * Identificar si procede
     * del teclado o del raton.
     */
    int source =
        (status & PS2_AUX_DATA)
            ? PS2_INPUT_MOUSE
            : PS2_INPUT_KEYBOARD;

    /*
     * Consumir exactamente un byte.
     */
    *data = ps2_inb(PS2_DATA_PORT);

    return source;
}
