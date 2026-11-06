
#include <stdint.h>
#include <stddef.h>

#include "net_udp.h"

#define IPV4_HEADER_SIZE 20U
#define UDP_HEADER_SIZE 8U
#define UDP_MAX_PAYLOAD 1472U

/* Escritura big-endian */
static void write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

/* Checksum de Internet */
static uint16_t checksum16(
    const uint8_t *data,
    uint32_t length
) {
    uint32_t sum = 0;

    for (uint32_t i = 0;
         i + 1U < length;
         i += 2U) {

        sum += ((uint32_t)data[i] << 8) |
               data[i + 1U];
    }

    if (length & 1U) {
        sum += (uint32_t)
            data[length - 1U] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

int net_udp_build_ipv4(
    uint8_t *out,
    uint32_t cap,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t *payload,
    uint16_t payload_size
) {
    if (!out || !src_ip || !dst_ip)
        return -1;

    if (payload_size && !payload)
        return -1;

    /*
     * Primera versión:
     * sin fragmentación IPv4.
     * MTU Ethernet habitual: 1500 bytes.
     */
    if (payload_size > UDP_MAX_PAYLOAD)
        return -1;

    uint32_t total =
        IPV4_HEADER_SIZE +
        UDP_HEADER_SIZE +
        payload_size;

    if (total > cap)
        return -1;

    for (uint32_t i = 0; i < total; i++)
        out[i] = 0;

    /* IPv4 */
    uint8_t *ip = out;

    ip[0] = 0x45;  /* Version 4, IHL 5 */
    write16(ip + 2, (uint16_t)total);
    write16(ip + 4, 0x1235);

    /* No fragmentar */
    write16(ip + 6, 0x4000);

    ip[8] = 64;    /* TTL */
    ip[9] = 17;    /* Protocolo UDP */

    for (uint32_t i = 0; i < 4U; i++) {
        ip[12U + i] = src_ip[i];
        ip[16U + i] = dst_ip[i];
    }

    uint16_t ip_hash = checksum16(ip, 20U);
    write16(ip + 10, ip_hash);

    /* Cabecera UDP */
    uint8_t *udp = out + IPV4_HEADER_SIZE;

    write16(udp + 0, src_port);
    write16(udp + 2, dst_port);

    write16(
        udp + 4,
        (uint16_t)(UDP_HEADER_SIZE + payload_size)
    );

    /*
     * UDP checksum = 0:
     * permitido para IPv4.
     *
     * Añadiremos checksum UDP completo
     * antes de generalizar el transporte.
     */
    write16(udp + 6, 0);

    for (uint32_t i = 0; i < payload_size; i++) {
        udp[UDP_HEADER_SIZE + i] = payload[i];
    }

    return (int)total;
}

/* Prueba sin acceder al hardware */
int net_udp_selftest(void) {
    uint8_t buffer[64];

    const uint8_t src[4] = {10, 0, 2, 15};
    const uint8_t dst[4] = {10, 0, 2, 3};

    const uint8_t message[4] = {
        'M', 'I', 'O', 'S'
    };

    int size = net_udp_build_ipv4(
        buffer,
        sizeof(buffer),
        src,
        dst,
        49152,
        53,
        message,
        sizeof(message)
    );

    /* IPv4 20 + UDP 8 + datos 4 */
    if (size != 32)
        return -1;

    /* Version IPv4 */
    if (buffer[0] != 0x45)
        return -1;

    /* Protocolo UDP = 17 */
    if (buffer[9] != 17)
        return -1;

    /* Longitud IP = 32 */
    if (buffer[2] != 0 ||
        buffer[3] != 32)
        return -1;

    /* Checksum IP válido */
    if (checksum16(buffer, 20U) != 0U)
        return -1;

    /* Puerto destino 53 */
    if (buffer[22] != 0 ||
        buffer[23] != 53)
        return -1;

    /* Longitud UDP = 12 */
    if (buffer[24] != 0 ||
        buffer[25] != 12)
        return -1;

    /* Comprobar datos */
    for (uint32_t i = 0; i < 4U; i++) {
        if (buffer[28U + i] != message[i])
            return -1;
    }

    /* Rechazar buffer insuficiente */
    if (net_udp_build_ipv4(
            buffer,
            10,
            src,
            dst,
            49152,
            53,
            message,
            4
        ) >= 0) {
        return -1;
    }

    return 0;
}
