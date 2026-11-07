
#ifndef MIOS_NET_HTTP_H
#define MIOS_NET_HTTP_H

#include <stdint.h>

/*
 * Construye una peticion HTTP/1.1 GET
 * para nuestro servidor de laboratorio.
 *
 * Retorna longitud o -1.
 */
int net_http_build_get(
    uint8_t *out,
    uint32_t capacity
);

int net_http_selftest(void);

/*
 * Resultado del analizador HTTP.
 *
 * body_offset indica donde empieza
 * el contenido dentro del buffer.
 */
typedef struct {
    uint16_t status_code;
    uint32_t header_length;
    uint32_t content_length;
    uint32_t body_offset;
    uint32_t body_length;
} net_http_response_t;

/*
 * Analiza HTTP/1.0 o HTTP/1.1.
 *
 * Soporta:
 * - Content-Length
 * - Un cuerpo completo en memoria
 *
 * No soporta Transfer-Encoding: chunked.
 *
 * Retorno:
 *   0 = respuesta valida y completa
 *  -1 = formato no soportado o invalido
 *  -2 = datos incompletos
 */
int net_http_parse_response(
    const uint8_t *data,
    uint32_t length,
    net_http_response_t *response
);

int net_http_parser_selftest(void);

#endif
