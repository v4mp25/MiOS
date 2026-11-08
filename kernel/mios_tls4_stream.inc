/*
 * MiOS Fase 4.1: TCP byte stream for a TLS engine.
 * Include AFTER net_public_09.inc in kernel/kernel.c.
 * Uses the working RTL8139+ARP+DNS+h08 TCP code, without changing DOWNLOAD 1.0.
 *
 * Status: experimental single connection; in-order + overlap trimming.
 * Not a general retransmitting TCP stack. Never use as a security claim.
 */
#ifndef MIOS_TLS4_STREAM_INC
#define MIOS_TLS4_STREAM_INC

#define TLS4_TCP_MSS 1200U
#define TLS4_STREAM_STASH 1600U
#define TLS4_RX_POLL_PACKETS 64U

static uint8_t tls4_stash[TLS4_STREAM_STASH];
static uint32_t tls4_stash_used;
static uint32_t tls4_stash_at;
static int tls4_tcp_opened;
static int tls4_tcp_fin_received;

static int tls4_host_ok(const char *host)
{
    uint32_t n = 0U;
    int dot = 0;
    if (!host) return 0;
    while (host[n]) {
        uint8_t c = (uint8_t)host[n];
        if (n >= 126U) return 0;
        if (c == '.') dot = 1;
        else if (!((c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-')) return 0;
        n++;
    }
    return n >= 4U && dot && host[0] != '.' && host[n - 1U] != '.';
}

static void tls4_tcp_reset(void)
{
    tls4_stash_used = 0U;
    tls4_stash_at = 0U;
    tls4_tcp_opened = 0;
    tls4_tcp_fin_received = 0;
}

/* Return 0 on success, -1 on failure. DNS is performed inside MiOS. */
static int tls4_tcp_connect_host(const char *host, uint16_t port)
{
    if (!tls4_host_ok(host) || port == 0U || tls4_tcp_opened) return -1;
    tls4_tcp_reset();
    uint32_t i = 0U;
    for (; host[i]; i++) {
        uint8_t c = (uint8_t)host[i];
        h08_host[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32U) : (char)c;
    }
    h08_host[i] = 0;

    if (h08_nic_start() != 0) return -1;
    if (h08_arp_resolve(h08_gateway_ip, h08_gateway_mac) != 0) return -1;
    if (h08_arp_resolve(h08_dns_ip, h08_dns_mac) != 0) return -1;
    if (h08_dns_get_a(h08_target_ip) != 0) return -1;
    h08_target_port = port;

    /* TCP initial sequence is NOT used as a TLS source of entropy. */
    uint32_t lo = 0U, hi = 0U;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    h08_iss = lo ^ (hi << 9U) ^ 0x6D494F53U;
    h08_local_port = (uint16_t)(50000U + (h08_session_attempts++ % 15000U));
    h08_send_next = 0U;
    h08_recv_next = 0U;
    if (h08_tcp_connect() != 0) return -1;
    tls4_tcp_opened = 1;
    serial_write("TLS4.1: TCP CONECTADO AL PUERTO 443\n");
    return 0;
}

/* Sends ALL input bytes, cutting each TCP segment at 1200. Fails closed. */
static int tls4_tcp_send_bytes(const uint8_t *data, uint32_t length)
{
    if (!tls4_tcp_opened || (length && !data) || length > 65536U) return -1;
    uint32_t pos = 0U;
    while (pos < length) {
        uint32_t chunk = length - pos;
        if (chunk > TLS4_TCP_MSS) chunk = TLS4_TCP_MSS;
        if (h08_tcp_send(0x18U, h08_send_next, h08_recv_next,
                         data + pos, chunk) != 0) {
            tls4_tcp_opened = 0;
            return -1;
        }
        h08_send_next += chunk;
        pos += chunk;
    }
    return (int)length;
}

static int tls4_tcp_copy_stash(uint8_t *out, uint32_t capacity)
{
    uint32_t available = tls4_stash_used - tls4_stash_at;
    if (available == 0U) return 0;
    if (capacity < available) available = capacity;
    for (uint32_t i = 0U; i < available; i++)
        out[i] = tls4_stash[tls4_stash_at + i];
    tls4_stash_at += available;
    if (tls4_stash_at == tls4_stash_used) {
        tls4_stash_at = 0U;
        tls4_stash_used = 0U;
    }
    return (int)available;
}

/*
 * Returns bytes read >0; 0 = peer FIN; -2 = no data yet; -1 = fatal.
 * Maintains unread bytes between calls, as required by TLS record parsing.
 */
static int tls4_tcp_recv_bytes(uint8_t *out, uint32_t capacity)
{
    if (!tls4_tcp_opened || !out || !capacity || capacity > 65536U) return -1;
    int n = tls4_tcp_copy_stash(out, capacity);
    if (n > 0) return n;
    if (tls4_tcp_fin_received) return 0;

    for (uint32_t k = 0U; k < TLS4_RX_POLL_PACKETS; k++) {
        int frame_len = h08_rx_poll();
        if (frame_len < 0) return -1;
        if (frame_len == 0) return -2;
        h08_tcp_info t;
        if (h08_tcp_parse(frame_len, &t) != 0) continue;
        if (t.flags & 0x04U) { tls4_tcp_opened = 0; return -1; }
        if (!(t.flags & 0x10U)) continue;
        /* Never accept data claiming ACK beyond our transmitted bytes. */
        if ((int32_t)(t.ack - h08_send_next) > 0) return -1;
        int32_t delta = (int32_t)(t.seq - h08_recv_next);
        if (delta > 0) {
            if (h08_tcp_send(0x10U, h08_send_next, h08_recv_next, 0, 0U) != 0)
                return -1;
            continue;
        }
        uint32_t trim = delta < 0 ? (uint32_t)(-delta) : 0U;
        if (trim > t.payload_size) trim = t.payload_size;
        uint32_t fresh = t.payload_size - trim;
        if (fresh > TLS4_STREAM_STASH) return -1;
        if (fresh) {
            for (uint32_t i = 0U; i < fresh; i++)
                tls4_stash[i] = t.payload[trim + i];
            tls4_stash_at = 0U;
            tls4_stash_used = fresh;
            h08_recv_next += fresh;
        }
        if ((t.flags & 0x01U) && t.seq + t.payload_size == h08_recv_next) {
            h08_recv_next++;
            tls4_tcp_fin_received = 1;
        }
        if (t.payload_size || (t.flags & 0x01U)) {
            if (h08_tcp_send(0x10U, h08_send_next, h08_recv_next, 0, 0U) != 0)
                return -1;
        }
        n = tls4_tcp_copy_stash(out, capacity);
        if (n > 0) return n;
        if (tls4_tcp_fin_received) return 0;
    }
    return -2;
}

/* Best-effort FIN; does not claim TIME_WAIT or retransmission support. */
static void tls4_tcp_close(void)
{
    if (tls4_tcp_opened) {
        if (h08_tcp_send(0x11U, h08_send_next, h08_recv_next, 0, 0U) == 0)
            h08_send_next++;
    }
    tls4_tcp_reset();
}

#endif
