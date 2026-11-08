/* Included after net_public_09.inc; stages 4.1/4.2 compile without Mbed TLS. */
#include "mios_tls4_platform.inc"
#include "mios_tls4_heap.inc"
#include "mios_tls4_stream.inc"
static int mios_tls4_check(void)
{
    serial_write("\n=== MiOS TLS 4.1-4.5: ESTADO ===\n");
    if (tls4_heap_selftest() != 0) {
        serial_write("TLS4.2: HEAP ERROR\n"); return -1;
    }
    serial_write("TLS4.2: HEAP 2 MIB OK (LAB)\n");
    if (tls4_platform_check() != 0) return -1;
    serial_write("TLS4.1: TCP STREAM ADAPTER PREPARADO\n");
    serial_write("TLS4.3: MBED TLS AUN NO ENLAZADO EN KERNEL\n");
    serial_write("TLS4.4/4.5: HANDSHAKE Y CERTIFICADO PENDIENTES\n");
    return 0;
}
static int mios_tls4_probe_tcp443(void)
{
    serial_write("\n=== MiOS TLS4.1: PROBAR TCP/443 ===\n");
    if (tls4_tcp_connect_host("example.com", 443U) != 0) {
        serial_write("TLS4.1: TCP443 ERROR\n"); return -1;
    }
    serial_write("TLS4.1: TCP443 OK (SIN TLS)\n");
    tls4_tcp_close();
    return 0;
}

/* 4.3-4.5 (optional): only compiled once the Mbed TLS port is genuinely linked. */
#include "mios_tls4_mbedtls36.inc"
