
#ifndef MIOS_NET_DNS_H
#define MIOS_NET_DNS_H

#include <stdint.h>

/*
 * Construye una consulta DNS tipo A (IPv4).
 *
 * Devuelve la longitud en bytes,
 * o -1 si los argumentos son invalidos.
 *
 * Solo crea el contenido DNS:
 * no incluye las cabeceras UDP/IP/Ethernet.
 */
int net_dns_build_query(
    uint8_t *out,
    uint32_t capacity,
    const char *domain,
    uint16_t transaction_id
);

int net_dns_selftest(void);

#endif
