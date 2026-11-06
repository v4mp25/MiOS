
#include <stdint.h>
#include "net_dns.h"

#define DNS_HEADER_SIZE 12U
#define DNS_MAX_NAME 253U

static void dns_write16(
    uint8_t *buffer,
    uint16_t value
) {
    buffer[0] = (uint8_t)(value >> 8);
    buffer[1] = (uint8_t)value;
}

/*
 * Convierte:
 * example.com
 *
 * En:
 * 07 example 03 com 00
 */
static int dns_encode_name(
    uint8_t *out,
    uint32_t capacity,
    const char *domain
) {
    if (!out || !domain)
        return -1;

    uint32_t written = 0;
    uint32_t input_pos = 0;
    uint32_t name_length = 0;

    if (domain[0] == '\0')
        return -1;

    while (domain[input_pos] != '\0') {
        uint32_t label_start = input_pos;
        uint32_t label_length = 0;

        while (domain[input_pos] != '\0' &&
               domain[input_pos] != '.') {

            uint8_t c =
                (uint8_t)domain[input_pos];

            if (c <= 32U || c >= 127U)
                return -1;

            if (label_length >= 63U)
                return -1;

            label_length++;
            input_pos++;
            name_length++;

            if (name_length > DNS_MAX_NAME)
                return -1;
        }

        if (label_length == 0U)
            return -1;

        if (written + 1U + label_length
            >= capacity)
            return -1;

        out[written++] =
            (uint8_t)label_length;

        for (uint32_t i = 0;
             i < label_length;
             i++) {
            out[written++] =
                (uint8_t)domain[label_start + i];
        }

        if (domain[input_pos] == '.') {
            input_pos++;

            if (domain[input_pos] == '\0')
                return -1;
        }
    }

    if (written >= capacity)
        return -1;

    out[written++] = 0;

    return (int)written;
}

int net_dns_build_query(
    uint8_t *out,
    uint32_t capacity,
    const char *domain,
    uint16_t transaction_id
) {
    if (!out || !domain ||
        capacity < DNS_HEADER_SIZE + 5U)
        return -1;

    for (uint32_t i = 0;
         i < capacity;
         i++) {
        out[i] = 0;
    }

    /* DNS Transaction ID */
    dns_write16(out + 0, transaction_id);

    /* Flags: consulta recursiva RD=1 */
    dns_write16(out + 2, 0x0100);

    /* QDCOUNT = 1 */
    dns_write16(out + 4, 1);

    /* ANCOUNT, NSCOUNT, ARCOUNT = 0 */

    int name_size = dns_encode_name(
        out + DNS_HEADER_SIZE,
        capacity - DNS_HEADER_SIZE,
        domain
    );

    if (name_size < 0)
        return -1;

    uint32_t pos =
        DNS_HEADER_SIZE + (uint32_t)name_size;

    if (pos + 4U > capacity)
        return -1;

    /* QTYPE = 1, registro A */
    dns_write16(out + pos, 1);
    pos += 2U;

    /* QCLASS = 1, Internet */
    dns_write16(out + pos, 1);
    pos += 2U;

    return (int)pos;
}

/*
 * Prueba sin acceso a la red.
 */
int net_dns_selftest(void) {
    uint8_t packet[128];

    int length = net_dns_build_query(
        packet,
        sizeof(packet),
        "example.com",
        0x4D49
    );

    /* 12 bytes DNS + 13 de nombre + 4 */
    if (length != 29)
        return -1;

    /* Transaction ID: MI */
    if (packet[0] != 0x4D ||
        packet[1] != 0x49)
        return -1;

    /* Flags: RD=1 */
    if (packet[2] != 0x01 ||
        packet[3] != 0x00)
        return -1;

    /* Una pregunta */
    if (packet[4] != 0x00 ||
        packet[5] != 0x01)
        return -1;

    /* Primer label = example (7) */
    if (packet[12] != 7)
        return -1;

    /* Segundo label = com (3) */
    if (packet[20] != 3)
        return -1;

    /* Fin del nombre */
    if (packet[24] != 0)
        return -1;

    /* QTYPE = A */
    if (packet[25] != 0 ||
        packet[26] != 1)
        return -1;

    /* QCLASS = IN */
    if (packet[27] != 0 ||
        packet[28] != 1)
        return -1;

    /* Rechazo de dominio invalido */
    if (net_dns_build_query(
            packet,
            sizeof(packet),
            "example..com",
            0x4D49
        ) >= 0)
        return -1;

    return 0;
}
