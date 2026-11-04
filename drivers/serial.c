
#include <stdint.h>

#include "serial.h"

/* Puerto base COM1 */
#define COM1 0x3F8U

static int initialized = 0;

static inline void outb(
    uint16_t port,
    uint8_t value
) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/*
 * Inicializar COM1 a 38400 baudios,
 * 8 bits de datos, sin paridad,
 * 1 bit de parada.
 */
int serial_init(void) {
    initialized = 0;

    /* Desactivar interrupciones UART */
    outb(COM1 + 1, 0x00);

    /* Activar divisor */
    outb(COM1 + 3, 0x80);

    /* Divisor 3 = 38400 baudios */
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);

    /* 8N1 */
    outb(COM1 + 3, 0x03);

    /* FIFO habilitado */
    outb(COM1 + 2, 0xC7);

    /* Activar salidas de control */
    outb(COM1 + 4, 0x0B);

    /*
     * Prueba loopback:
     * transmitir y recibir el mismo byte.
     */
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0xAE);

    uint32_t timeout = 100000U;

    while (timeout-- > 0U) {
        if (inb(COM1 + 5) & 0x01U)
            break;
    }

    if ((inb(COM1 + 5) & 0x01U) == 0U) {
        outb(COM1 + 4, 0x00);
        return -1;
    }

    if (inb(COM1 + 0) != 0xAE) {
        outb(COM1 + 4, 0x00);
        return -1;
    }

    /* Regresar al modo normal */
    outb(COM1 + 4, 0x0F);

    initialized = 1;
    return 0;
}

int serial_available(void) {
    return initialized;
}

void serial_write_char(char c) {
    if (!initialized)
        return;

    /*
     * Esperar a que pueda transmitirse.
     * Se usa un limite para evitar
     * bloquear indefinidamente el kernel.
     */
    uint32_t timeout = 1000000U;

    while ((inb(COM1 + 5) & 0x20U) == 0U) {
        if (timeout-- == 0U)
            return;
    }

    outb(COM1, (uint8_t)c);
}

void serial_write(const char *text) {
    if (text == 0)
        return;

    while (*text) {
        if (*text == '\n')
            serial_write_char('\r');

        serial_write_char(*text++);
    }
}

void serial_write_hex32(uint32_t value) {
    static const char hex[] =
        "0123456789ABCDEF";

    serial_write("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_write_char(
            hex[(value >> shift) & 0xFU]
        );
    }
}

