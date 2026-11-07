
#ifndef MIOS_NET_TCP_H
#define MIOS_NET_TCP_H

#include <stdint.h>

/*
 * Construir paquete IPv4 + TCP SYN.
 *
 * No incluye cabecera Ethernet.
 * No realiza transmision real.
 *
 * Devuelve 40 bytes o -1 si falla.
 */
int net_tcp_build_syn(
    uint8_t *out,
    uint32_t capacity,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t sequence_number
);

/* Pruebas de construccion y checksums */
int net_tcp_selftest(void);

/*
 * MiOS TCP 0.4
 * Construir IPv4 + TCP ACK sin datos.
 *
 * Devuelve 40 bytes o -1.
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
);

#endif
