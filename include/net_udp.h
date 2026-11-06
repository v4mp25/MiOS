
#ifndef MIOS_NET_UDP_H
#define MIOS_NET_UDP_H

#include <stdint.h>

/*
 * Construir IPv4 + UDP en memoria.
 *
 * out: buffer de salida
 * cap: capacidad disponible
 * src_ip/dst_ip: cuatro bytes de IPv4
 *
 * Devuelve el tamaño del paquete
 * o -1 si hay un error.
 *
 * No envía datos por la tarjeta.
 */
int net_udp_build_ipv4(
    uint8_t *out,
    uint32_t cap,
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t *payload,
    uint16_t payload_size
);

/* Prueba del protocolo en RAM */
int net_udp_selftest(void);

#endif
