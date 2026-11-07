
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

/*
 * MiOS HTTP 0.4
 *
 * Analizador de respuestas HTTP pequeñas.
 * Sin malloc y sin libc.
 */

static uint8_t http_lower(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(c + 32U);

    return c;
}

static int http_name_equals(
    const uint8_t *data,
    uint32_t length,
    const char *name
) {
    uint32_t i = 0U;

    while (name[i] != 0) {
        if (i >= length)
            return 0;

        if (http_lower(data[i]) !=
            http_lower((uint8_t)name[i]))
            return 0;

        i++;
    }

    return i == length;
}

int net_http_parse_response(
    const uint8_t *data,
    uint32_t length,
    net_http_response_t *response
) {
    if (!data || !response)
        return -1;

    response->status_code = 0U;
    response->header_length = 0U;
    response->content_length = 0U;
    response->body_offset = 0U;
    response->body_length = 0U;

    if (length > 65535U)
        return -1;

    /*
     * Encontrar CRLFCRLF.
     */
    uint32_t body_offset = 0U;

    for (uint32_t i = 0U;
         i + 3U < length;
         i++) {

        if (data[i] == '\r' &&
            data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' &&
            data[i + 3U] == '\n') {

            body_offset = i + 4U;
            break;
        }
    }

    if (body_offset == 0U)
        return -2;

    /*
     * Primera linea:
     * HTTP/1.1 200 OK
     */
    if (body_offset < 16U)
        return -1;

    int version_ok = 1;

    const char version[] = "HTTP/1.";

    for (uint32_t i = 0U; i < 7U; i++) {
        if (data[i] != (uint8_t)version[i]) {
            version_ok = 0;
            break;
        }
    }

    if (!version_ok ||
        (data[7] != '0' && data[7] != '1') ||
        data[8] != ' ')
        return -1;

    if (data[9] < '0' || data[9] > '9' ||
        data[10] < '0' || data[10] > '9' ||
        data[11] < '0' || data[11] > '9')
        return -1;

    uint16_t status =
        (uint16_t)(
            (data[9] - '0') * 100U +
            (data[10] - '0') * 10U +
            (data[11] - '0')
        );

    if (status < 100U || status > 599U)
        return -1;

    if (data[12] != ' ' &&
        data[12] != '\r')
        return -1;

    /*
     * Encontrar final de linea del estado.
     */
    uint32_t pos = 0U;

    while (pos + 1U < body_offset) {
        if (data[pos] == '\r' &&
            data[pos + 1U] == '\n')
            break;

        pos++;
    }

    if (pos + 1U >= body_offset)
        return -1;

    pos += 2U;

    /*
     * Analizar cabeceras linea por linea.
     */
    uint32_t content_length = 0U;
    int found_length = 0;

    while (pos + 1U < body_offset) {

        uint32_t line_start = pos;

        while (pos + 1U < body_offset &&
               !(data[pos] == '\r' &&
                 data[pos + 1U] == '\n')) {
            pos++;
        }

        if (pos + 1U >= body_offset)
            return -1;

        uint32_t line_end = pos;
        pos += 2U;

        if (line_start == line_end)
            break;

        uint32_t colon = line_start;

        while (colon < line_end &&
               data[colon] != ':')
            colon++;

        if (colon == line_end)
            return -1;

        uint32_t name_length =
            colon - line_start;

        if (http_name_equals(
                data + line_start,
                name_length,
                "Transfer-Encoding"
            )) {

            /* Chunked no implementado */
            return -1;
        }

        if (!http_name_equals(
                data + line_start,
                name_length,
                "Content-Length"
            ))
            continue;

        /* Evitar cabeceras duplicadas */
        if (found_length)
            return -1;

        found_length = 1;

        uint32_t value_pos = colon + 1U;

        while (value_pos < line_end &&
               (data[value_pos] == ' ' ||
                data[value_pos] == '\t'))
            value_pos++;

        if (value_pos == line_end)
            return -1;

        uint32_t value = 0U;
        uint32_t digits = 0U;

        while (value_pos < line_end &&
               data[value_pos] >= '0' &&
               data[value_pos] <= '9') {

            uint32_t digit =
                data[value_pos] - '0';

            /* Evitar overflow */
            if (value > (65535U - digit) / 10U)
                return -1;

            value = value * 10U + digit;

            value_pos++;
            digits++;
        }

        if (digits == 0U)
            return -1;

        while (value_pos < line_end &&
               (data[value_pos] == ' ' ||
                data[value_pos] == '\t'))
            value_pos++;

        if (value_pos != line_end)
            return -1;

        content_length = value;
    }

    /*
     * Por ahora requerimos Content-Length.
     */
    if (!found_length)
        return -1;

    uint32_t available =
        length - body_offset;

    if (available < content_length)
        return -2;

    /*
     * La prueba espera un mensaje completo
     * sin bytes adicionales.
     */
    if (available != content_length)
        return -1;

    response->status_code = status;
    response->header_length = body_offset;
    response->content_length = content_length;
    response->body_offset = body_offset;
    response->body_length = content_length;

    return 0;
}

/*
 * MiOS HTTP 0.4
 * Pruebas sin conexion de red.
 */
int net_http_parser_selftest(void)
{
    static const uint8_t sample[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 17\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Bienvenido a MiOS";

    net_http_response_t response;

    int result = net_http_parse_response(
        sample,
        sizeof(sample) - 1U,
        &response
    );

    if (result != 0)
        return -1;

    if (response.status_code != 200U)
        return -1;

    if (response.content_length != 17U)
        return -1;

    if (response.body_length != 17U)
        return -1;

    static const char expected[] =
        "Bienvenido a MiOS";

    for (uint32_t i = 0U; i < 17U; i++) {
        if (sample[response.body_offset + i] !=
            (uint8_t)expected[i])
            return -1;
    }

    /*
     * Una respuesta truncada debe
     * detectarse como incompleta.
     */
    if (net_http_parse_response(
            sample,
            sizeof(sample) - 2U,
            &response
        ) != -2)
        return -1;

    /*
     * No aceptar contenido sin longitud.
     */
    static const uint8_t no_length[] =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hola";

    if (net_http_parse_response(
            no_length,
            sizeof(no_length) - 1U,
            &response
        ) != -1)
        return -1;

    return 0;
}
