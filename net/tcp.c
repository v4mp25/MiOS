
#include <stdint.h>
#include "net_tcp.h"

#define TCP_IP_HEADER 20U
#define TCP_HEADER    20U
#define TCP_SYN_SIZE  40U

static void tcp_write16(
    uint8_t *p,
    uint16_t value
) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void tcp_write32(
    uint8_t *p,
    uint32_t value
) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* Suma de palabras de 16 bits, formato red */
static uint32_t tcp_sum_bytes(
    const uint8_t *data,
    uint32_t length
) {
    uint32_t sum = 0;

    for (uint32_t i = 0;
         i + 1U < length;
         i += 2U) {

        sum +=
            ((uint32_t)data[i] << 8) |
            data[i + 1U];
    }

    if (length & 1U) {
        sum +=
            (uint32_t)data[length - 1U] << 8;
    }

    return sum;
}

static uint16_t tcp_finish_checksum(
    uint32_t sum
) {
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) +
              (sum >> 16);
    }

    return (uint16_t)~sum;
}

/*
 * Checksum TCP:
 * pseudo-cabecera IPv4 + segmento TCP.
 */
static uint16_t tcp_checksum(
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    const uint8_t *tcp,
    uint16_t tcp_length
) {
    uint32_t sum = 0;

    sum += tcp_sum_bytes(src_ip, 4U);
    sum += tcp_sum_bytes(dst_ip, 4U);

    /* Protocolo TCP = 6 */
    sum += 6U;

    /* Longitud TCP en pseudo-cabecera */
    sum += tcp_length;

    sum += tcp_sum_bytes(
        tcp,
        tcp_length
    );

    return tcp_finish_checksum(sum);
}

int net_tcp_build_syn(
    uint8_t *out,
    uint32_t capacity,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t sequence_number
) {
    if (!out || !src_ip || !dst_ip ||
        capacity < TCP_SYN_SIZE ||
        src_port == 0U ||
        dst_port == 0U) {
        return -1;
    }

    /* Limpiar el paquete */
    for (uint32_t i = 0;
         i < TCP_SYN_SIZE;
         i++) {
        out[i] = 0;
    }

    /*
     * IPv4: 20 bytes
     */
    uint8_t *ip = out;

    ip[0] = 0x45;   /* IPv4, IHL=5 */
    ip[1] = 0x00;

    tcp_write16(ip + 2, TCP_SYN_SIZE);
    tcp_write16(ip + 4, 0x1236);

    /* Don't Fragment */
    tcp_write16(ip + 6, 0x4000);

    ip[8] = 64;     /* TTL */
    ip[9] = 6;      /* TCP */

    for (uint32_t i = 0; i < 4U; i++) {
        ip[12U + i] = src_ip[i];
        ip[16U + i] = dst_ip[i];
    }

    /* Checksum IPv4 */
    tcp_write16(
        ip + 10,
        tcp_finish_checksum(
            tcp_sum_bytes(ip, TCP_IP_HEADER)
        )
    );

    /*
     * TCP: 20 bytes
     */
    uint8_t *tcp = out + TCP_IP_HEADER;

    tcp_write16(tcp + 0, src_port);
    tcp_write16(tcp + 2, dst_port);

    tcp_write32(
        tcp + 4,
        sequence_number
    );

    /* ACK number = 0 para SYN inicial */
    tcp_write32(tcp + 8, 0U);

    /* Data offset: 5 palabras (20 bytes) */
    tcp[12] = 0x50;

    /* Flag SYN */
    tcp[13] = 0x02;

    /* Receive window */
    tcp_write16(tcp + 14, 4096U);

    /* Checksum TCP */
    tcp_write16(
        tcp + 16,
        tcp_checksum(
            src_ip,
            dst_ip,
            tcp,
            TCP_HEADER
        )
    );

    /* Urgent pointer = 0 */
    tcp_write16(tcp + 18, 0U);

    return (int)TCP_SYN_SIZE;
}

/*
 * Prueba interna sin hardware.
 */
int net_tcp_selftest(void) {
    uint8_t packet[40];

    const uint8_t src[4] = {
        10, 0, 2, 15
    };

    const uint8_t dst[4] = {
        10, 0, 2, 2
    };

    int length = net_tcp_build_syn(
        packet,
        sizeof(packet),
        src,
        dst,
        49154U,
        8080U,
        0x12345678U
    );

    if (length != 40)
        return -1;

    /* IPv4 + TCP */
    if (packet[0] != 0x45 ||
        packet[9] != 6)
        return -1;

    /* Longitud IP = 40 */
    if (packet[2] != 0 ||
        packet[3] != 40)
        return -1;

    /* Header IPv4 checksum correcto */
    if (tcp_finish_checksum(
            tcp_sum_bytes(packet, 20U)
        ) != 0U)
        return -1;

    /* Puertos 49154 -> 8080 */
    if (packet[20] != 0xC0 ||
        packet[21] != 0x02 ||
        packet[22] != 0x1F ||
        packet[23] != 0x90)
        return -1;

    /* Sequence number */
    if (packet[24] != 0x12 ||
        packet[25] != 0x34 ||
        packet[26] != 0x56 ||
        packet[27] != 0x78)
        return -1;

    /* TCP header size y SYN */
    if (packet[32] != 0x50 ||
        packet[33] != 0x02)
        return -1;

    /* Checksum TCP correcto */
    if (tcp_checksum(
            src,
            dst,
            packet + 20,
            TCP_HEADER
        ) != 0U)
        return -1;

    /*
     * Comprobar que un cambio de datos
     * invalida el checksum.
     */
    packet[24] ^= 1U;

    if (tcp_checksum(
            src,
            dst,
            packet + 20,
            TCP_HEADER
        ) == 0U)
        return -1;

    /* Restaurar */
    packet[24] ^= 1U;

    /* Rechazar buffer insuficiente */
    if (net_tcp_build_syn(
            packet,
            10U,
            src,
            dst,
            49154U,
            8080U,
            0x12345678U
        ) >= 0)
        return -1;

    /*
     * TCP 0.4: comprobar paquete ACK.
     */
    int ack_size = net_tcp_build_ack(
        packet,
        sizeof(packet),
        src,
        dst,
        49154U,
        8080U,
        0x12345679U,
        0x0000FA02U
    );

    if (ack_size != 40)
        return -1;

    /* ACK activo, SYN apagado */
    if (packet[33] != 0x10U)
        return -1;

    /* Nuestro SEQ */
    if (packet[24] != 0x12U ||
        packet[25] != 0x34U ||
        packet[26] != 0x56U ||
        packet[27] != 0x79U)
        return -1;

    /* ACK del servidor */
    if (packet[28] != 0x00U ||
        packet[29] != 0x00U ||
        packet[30] != 0xFAU ||
        packet[31] != 0x02U)
        return -1;

    /* Checksum IPv4 */
    if (tcp_finish_checksum(
            tcp_sum_bytes(packet, 20U)
        ) != 0U)
        return -1;

    /* Checksum TCP */
    if (tcp_checksum(
            src,
            dst,
            packet + 20,
            20U
        ) != 0U)
        return -1;

    /*
     * TCP 0.5A: comprobar datos PSH/ACK.
     */
    const uint8_t tcp_message[] = {
        'H', 'O', 'L', 'A', ' ',
        'T', 'C', 'P', ' ',
        'M', 'I', 'O', 'S'
    };

    uint8_t data_packet[64];

    int data_size = net_tcp_build_data(
        data_packet,
        sizeof(data_packet),
        src,
        dst,
        49154U,
        8080U,
        0x12345679U,
        0x0000FA02U,
        tcp_message,
        sizeof(tcp_message)
    );

    if (data_size != 53)
        return -1;

    /* IPv4 total = 53 */
    if (data_packet[2] != 0U ||
        data_packet[3] != 53U)
        return -1;

    /* PSH + ACK */
    if (data_packet[33] != 0x18U)
        return -1;

    /* Verificar los 13 bytes */
    for (uint32_t i = 0;
         i < sizeof(tcp_message);
         i++) {

        if (data_packet[40U + i] != tcp_message[i])
            return -1;
    }

    /* Checksum IPv4 */
    if (tcp_finish_checksum(
            tcp_sum_bytes(data_packet, 20U)
        ) != 0U)
        return -1;

    /* Checksum TCP incluyendo payload */
    if (tcp_checksum(
            src,
            dst,
            data_packet + 20,
            33U
        ) != 0U)
        return -1;

    return 0;
}

/*
 * MiOS Network TCP 0.4
 *
 * Construye un paquete IPv4 + TCP ACK.
 * Sin opciones y sin datos.
 */
int net_tcp_build_ack(
    uint8_t *out,
    uint32_t capacity,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t sequence_number,
    uint32_t acknowledgment_number
) {
    if (!out || !src_ip || !dst_ip ||
        capacity < 40U ||
        src_port == 0U ||
        dst_port == 0U)
        return -1;

    for (uint32_t i = 0; i < 40U; i++)
        out[i] = 0;

    /* Cabecera IPv4 */
    uint8_t *ip = out;

    ip[0] = 0x45;
    ip[1] = 0x00;

    tcp_write16(ip + 2, 40U);
    tcp_write16(ip + 4, 0x1237U);
    tcp_write16(ip + 6, 0x4000U);

    ip[8] = 64U;
    ip[9] = 6U;

    for (uint32_t i = 0; i < 4U; i++) {
        ip[12U + i] = src_ip[i];
        ip[16U + i] = dst_ip[i];
    }

    /* Checksum de IPv4 */
    tcp_write16(
        ip + 10,
        tcp_finish_checksum(
            tcp_sum_bytes(ip, 20U)
        )
    );

    /* Cabecera TCP */
    uint8_t *tcp = out + 20U;

    tcp_write16(tcp + 0, src_port);
    tcp_write16(tcp + 2, dst_port);

    /* Nuestro siguiente SEQ */
    tcp_write32(
        tcp + 4,
        sequence_number
    );

    /* Siguiente SEQ esperado del servidor */
    tcp_write32(
        tcp + 8,
        acknowledgment_number
    );

    /* Cabecera TCP de 20 bytes */
    tcp[12] = 0x50;

    /* Solo bandera ACK */
    tcp[13] = 0x10;

    /* Ventana anunciada */
    tcp_write16(tcp + 14, 4096U);

    /* Checksum TCP obligatorio */
    tcp_write16(
        tcp + 16,
        tcp_checksum(
            src_ip,
            dst_ip,
            tcp,
            20U
        )
    );

    return 40;
}

/*
 * MiOS TCP 0.5A
 *
 * Construir IPv4 + TCP PSH/ACK.
 * Sin opciones TCP, sin fragmentacion.
 */
int net_tcp_build_data(
    uint8_t *out,
    uint32_t capacity,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t sequence_number,
    uint32_t acknowledgment_number,
    const uint8_t *data,
    uint16_t data_length
) {
    if (!out || !src_ip || !dst_ip ||
        !data || data_length == 0U ||
        data_length > 1460U ||
        src_port == 0U || dst_port == 0U)
        return -1;

    uint32_t total_size = 40U + data_length;
    uint16_t tcp_size = (uint16_t)(20U + data_length);

    if (capacity < total_size)
        return -1;

    for (uint32_t i = 0; i < total_size; i++)
        out[i] = 0;

    /* IPv4 */
    uint8_t *ip = out;

    ip[0] = 0x45;
    ip[1] = 0;

    tcp_write16(ip + 2, (uint16_t)total_size);
    tcp_write16(ip + 4, 0x1238U);
    tcp_write16(ip + 6, 0x4000U);

    ip[8] = 64U;
    ip[9] = 6U;

    for (uint32_t i = 0; i < 4U; i++) {
        ip[12U + i] = src_ip[i];
        ip[16U + i] = dst_ip[i];
    }

    tcp_write16(
        ip + 10,
        tcp_finish_checksum(
            tcp_sum_bytes(ip, 20U)
        )
    );

    /* TCP */
    uint8_t *tcp = out + 20U;

    tcp_write16(tcp + 0, src_port);
    tcp_write16(tcp + 2, dst_port);

    tcp_write32(tcp + 4, sequence_number);
    tcp_write32(tcp + 8, acknowledgment_number);

    /* TCP header = 20 bytes */
    tcp[12] = 0x50;

    /* PSH + ACK */
    tcp[13] = 0x18;

    tcp_write16(tcp + 14, 4096U);

    /* Copiar datos despues del header TCP */
    for (uint32_t i = 0; i < data_length; i++) {
        tcp[20U + i] = data[i];
    }

    /* Checksum TCP incluye datos */
    tcp_write16(
        tcp + 16,
        tcp_checksum(
            src_ip,
            dst_ip,
            tcp,
            tcp_size
        )
    );

    return (int)total_size;
}
