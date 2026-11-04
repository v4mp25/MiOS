
#include <stdint.h>

#include "mouse.h"

#define PS2_DATA      0x60U
#define PS2_STATUS    0x64U

#define PS2_OUTPUT_FULL 0x01U
#define PS2_INPUT_FULL  0x02U
#define PS2_AUX_DATA    0x20U

#define PS2_TIMEOUT 1000000U

/* Estado del raton */
static MouseState mouse_state = {
    .x = 320,
    .y = 240,
    .left = 0,
    .right = 0,
    .middle = 0,
    .available = 0,
    .packet_count = 0
};

/* Paquete PS/2 de tres bytes */
static uint8_t packet[3];
static uint8_t packet_index = 0;
static uint32_t completed_packets = 0;

static uint8_t mouse_inb(uint16_t port) {
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void mouse_outb(
    uint16_t port,
    uint8_t value
) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

/* Esperar hasta poder escribir */
static int ps2_wait_write(void) {
    for (uint32_t i = 0;
         i < PS2_TIMEOUT;
         i++) {

        if (!(mouse_inb(PS2_STATUS)
              & PS2_INPUT_FULL)) {
            return 0;
        }
    }

    return -1;
}

/* Esperar un byte disponible */
static int ps2_wait_read(uint8_t *value) {
    if (!value)
        return -1;

    for (uint32_t i = 0;
         i < PS2_TIMEOUT;
         i++) {

        if (mouse_inb(PS2_STATUS)
            & PS2_OUTPUT_FULL) {

            *value = mouse_inb(PS2_DATA);
            return 0;
        }
    }

    return -1;
}

/* Enviar un comando al controlador */
static int ps2_command(uint8_t command) {
    if (ps2_wait_write() != 0)
        return -1;

    mouse_outb(PS2_STATUS, command);
    return 0;
}

/* Enviar un dato al controlador */
static int ps2_write_data(uint8_t value) {
    if (ps2_wait_write() != 0)
        return -1;

    mouse_outb(PS2_DATA, value);
    return 0;
}

/*
 * Enviar un comando al raton.
 *
 * El prefijo D4 selecciona el
 * segundo puerto PS/2.
 */
static int mouse_command(uint8_t value) {
    uint8_t response = 0;

    if (ps2_command(0xD4U) != 0)
        return -1;

    if (ps2_write_data(value) != 0)
        return -1;

    /* Esperar respuesta del raton */
    for (uint32_t i = 0;
         i < PS2_TIMEOUT;
         i++) {

        uint8_t status = mouse_inb(PS2_STATUS);

        if (!(status & PS2_OUTPUT_FULL))
            continue;

        uint8_t data = mouse_inb(PS2_DATA);

        if (!(status & PS2_AUX_DATA))
            continue;

        response = data;
        break;
    }

    /* 0xFA = ACK */
    return response == 0xFAU ? 0 : -1;
}

int mouse_init(void) {
    uint8_t config = 0;

    mouse_state.x = 320;
    mouse_state.y = 240;
    mouse_state.left = 0;
    mouse_state.right = 0;
    mouse_state.middle = 0;
    mouse_state.available = 0;

    packet_index = 0;
    completed_packets = 0;
mouse_state.packet_count = 0;

    /*
     * Habilitar el segundo puerto PS/2.
     */
    if (ps2_command(0xA8U) != 0)
        return -1;

    /*
     * Leer configuracion del 8042.
     */
    if (ps2_command(0x20U) != 0)
        return -1;

    if (ps2_wait_read(&config) != 0)
        return -1;

    /*
     * Mantener IRQ12 deshabilitada
     * durante el modo de sondeo.
     *
     * Bit 1 = IRQ12
     * Bit 5 = reloj auxiliar desactivado
     */
    config &= (uint8_t)~0x22U;

    if (ps2_command(0x60U) != 0)
        return -1;

    if (ps2_write_data(config) != 0)
        return -1;

    /*
     * Valores predeterminados:
     * raton de 3 botones, paquetes
     * de tres bytes.
     */
    if (mouse_command(0xF6U) != 0)
        return -1;

    /*
     * Activar envio de movimientos.
     */
    if (mouse_command(0xF4U) != 0)
        return -1;

    mouse_state.available = 1;
    return 0;
}

void mouse_handle_byte(uint8_t data) {
    if (!mouse_state.available)
        return;

    /*
     * Primer byte:
     * el bit 3 debe ser 1.
     */
    if (packet_index == 0) {
        if ((data & 0x08U) == 0U)
            return;
    }

    packet[packet_index++] = data;

    if (packet_index < 3)
        return;

    packet_index = 0;

completed_packets++;
mouse_state.packet_count = completed_packets;

uint8_t flags = packet[0];
    /*
     * Ignorar movimientos con
     * desbordamiento.
     */
    if (flags & 0xC0U)
        return;

    /*
     * Desplazamientos relativos
     * con signo de nueve bits.
     */
    int32_t dx = (int32_t)packet[1];
    int32_t dy = (int32_t)packet[2];

    if (flags & 0x10U)
        dx -= 256;

    if (flags & 0x20U)
        dy -= 256;

    /*
     * Coordenadas de pantalla.
     * En PS/2 el Y positivo apunta
     * hacia arriba.
     */
    mouse_state.x += dx;
    mouse_state.y -= dy;

    /* Limitar a 640 x 480 */
    if (mouse_state.x < 0)
        mouse_state.x = 0;

    if (mouse_state.x > 639)
        mouse_state.x = 639;

    if (mouse_state.y < 0)
        mouse_state.y = 0;

    if (mouse_state.y > 479)
        mouse_state.y = 479;

    /* Botones */
    mouse_state.left =
        (flags & 0x01U) ? 1U : 0U;

    mouse_state.right =
        (flags & 0x02U) ? 1U : 0U;

    mouse_state.middle =
        (flags & 0x04U) ? 1U : 0U;
}

void mouse_get_state(MouseState *state) {
    if (!state)
        return;

    *state = mouse_state;
}
