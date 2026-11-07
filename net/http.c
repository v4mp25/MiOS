
#include <stdint.h>
#include "net_http.h"

/*
 * MiOS HTTP 0.1
 *
 * Peticion HTTP para servidor
 * de laboratorio 10.0.2.2:8080.
 */
static const char http_request[] =
    "GET / HTTP/1.1\r\n"
    "Host: 10.0.2.2:8080\r\n"
    "Connection: close\r\n"
    "\r\n";

#define HTTP_REQUEST_SIZE \
    ((uint32_t)(sizeof(http_request) - 1U))

int net_http_build_get(
    uint8_t *out,
    uint32_t capacity
) {
    if (out == 0 ||
        capacity < HTTP_REQUEST_SIZE)
        return -1;

    for (uint32_t i = 0;
         i < HTTP_REQUEST_SIZE;
         i++) {
        out[i] = (uint8_t)http_request[i];
    }

    return (int)HTTP_REQUEST_SIZE;
}

int net_http_selftest(void) {
    uint8_t packet[128];

    int size = net_http_build_get(
        packet,
        sizeof(packet)
    );

    if (size != (int)HTTP_REQUEST_SIZE)
        return -1;

    for (uint32_t i = 0;
         i < HTTP_REQUEST_SIZE;
         i++) {

        if (packet[i] !=
            (uint8_t)http_request[i])
            return -1;
    }

    /* Comprobar metodo GET */
    if (packet[0] != 'G' ||
        packet[1] != 'E' ||
        packet[2] != 'T' ||
        packet[3] != ' ')
        return -1;

    /* Comprobar fin de cabeceras */
    uint32_t n = HTTP_REQUEST_SIZE;

    if (packet[n - 4U] != '\r' ||
        packet[n - 3U] != '\n' ||
        packet[n - 2U] != '\r' ||
        packet[n - 1U] != '\n')
        return -1;

    /* No escribir fuera del buffer */
    if (net_http_build_get(
            packet, 8U
        ) >= 0)
        return -1;

    return 0;
}
