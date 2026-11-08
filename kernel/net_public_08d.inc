/*
 * MiOS HTTP 0.8A-E -- public HTTP, BINFS downloads, variable URLs.
 * Included by kernel/kernel.c immediately before rtl8139_dma_arp_test.
 * Also supports an OPTIONAL host-verified HTTPS bridge on 10.0.2.2:8081.
 * This bridge is NOT native TLS in MiOS; never claim otherwise.
 *
 * One request per boot; in-order TCP + duplicate trimming. No native TLS,
 * DHCP, IP fragmentation, or retransmission timer. Maximum file 8192 B.
 */

#define H08_RX_MAX_FRAME 1600U
#define H08_TX_MAX_FRAME 1600U
#define H08_CAPTURE_MAX  12288U
#define H08_POLL_LIMIT  100000000U

static const uint8_t h08_my_ip[4] = {10, 0, 2, 15};
static const uint8_t h08_gateway_ip[4] = {10, 0, 2, 2};
static const uint8_t h08_dns_ip[4] = {10, 0, 2, 3};
/* Runtime HTTP URL: plain HTTP is native; HTTPS uses optional WSL bridge. */
static char h08_host[128] = "example.com";
static char h08_path[256] = "/";
static char h08_url[392] = "http://example.com/";
static uint16_t h08_target_port = 80U;
static int h08_bridge_mode = 0;
static int h08_url_configured = 0;
static uint8_t h08_body[BINFS_MAX_FILE_SIZE];
static uint8_t h08_disk_verify[BINFS_MAX_FILE_SIZE];
static uint32_t h08_body_size;
static char h08_filename[32];

static uint8_t h08_tx_frames[4][H08_TX_MAX_FRAME]
    __attribute__((aligned(4096)));
static uint8_t h08_rx_frame[H08_RX_MAX_FRAME];
static uint8_t h08_capture[H08_CAPTURE_MAX + 1U];
static uint8_t h08_gateway_mac[6];
static uint8_t h08_dns_mac[6];
static uint8_t h08_target_ip[4];
static uint8_t h08_slot_used[4];
static uint32_t h08_tx_slot;
static uint32_t h08_rx_offset;
static uint16_t h08_ip_id;
static uint16_t h08_local_port;
static uint32_t h08_iss;
static uint32_t h08_send_next;
static uint32_t h08_recv_next;
static int h08_test_used;

static uint16_t h08_get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t h08_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void h08_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void h08_put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int h08_ip_equal(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < 4U; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static uint32_t h08_sum_bytes(const uint8_t *p, uint32_t n)
{
    uint32_t sum = 0;
    uint32_t i = 0;
    while (i + 1U < n) {
        sum += ((uint32_t)p[i] << 8) | p[i + 1U];
        i += 2U;
    }
    if (i < n) sum += (uint32_t)p[i] << 8;
    return sum;
}

static uint16_t h08_finish_sum(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t h08_transport_sum(const uint8_t src[4],
                                  const uint8_t dst[4],
                                  uint8_t proto, const uint8_t *segment,
                                  uint16_t segment_length)
{
    uint32_t sum = h08_sum_bytes(src, 4U);
    sum += h08_sum_bytes(dst, 4U);
    sum += (uint32_t)proto + segment_length;
    sum += h08_sum_bytes(segment, segment_length);
    return h08_finish_sum(sum);
}

static void h08_print_ip(const uint8_t ip[4])
{
    uint32_t encoded = ((uint32_t)ip[0] << 24) |
                       ((uint32_t)ip[1] << 16) |
                       ((uint32_t)ip[2] << 8) | ip[3];
    serial_write_hex32(encoded);
}

/* Allocate a completed TX descriptor; each descriptor owns its DMA buffer. */
static uint8_t *h08_tx_prepare(void)
{
    uint32_t slot = h08_tx_slot;
    if (h08_slot_used[slot]) {
        uint32_t tsd = rtl8139_in32(
            (uint16_t)(rtl8139_io_base + 0x10U + slot * 4U));
        if ((tsd & 0x00008000U) == 0U) return 0;
    }
    uint8_t *p = h08_tx_frames[slot];
    for (uint32_t i = 0; i < H08_TX_MAX_FRAME; i++) p[i] = 0;
    return p;
}

static int h08_tx_submit(uint32_t size)
{
    if (size > H08_TX_MAX_FRAME) return -1;
    if (size < 60U) size = 60U;
    uint32_t slot = h08_tx_slot;
    uint16_t io = rtl8139_io_base;
    uintptr_t addr = (uintptr_t)&h08_tx_frames[slot][0];
    if (addr >= (uintptr_t)(0x08000000U - H08_TX_MAX_FRAME))
        return -1; /* Current MiOS kernel uses identity map for low 128 MiB. */

    __asm__ volatile("" ::: "memory");
    rtl8139_out32((uint16_t)(io + 0x20U + slot * 4U),
                  (uint32_t)addr);
    rtl8139_out32((uint16_t)(io + 0x10U + slot * 4U), size);
    h08_slot_used[slot] = 1U;

    for (uint32_t i = 0; i < 5000000U; i++) {
        uint32_t tsd = rtl8139_in32(
            (uint16_t)(io + 0x10U + slot * 4U));
        if (tsd & 0x00008000U) {
            h08_tx_slot = (slot + 1U) & 3U;
            return 0;
        }
        if (tsd & 0x40000000U) return -1;
    }
    serial_write("HTTP08: TX TIMEOUT, STATUS=");
    serial_write_hex32(rtl8139_in32(
        (uint16_t)(io + 0x10U + slot * 4U)));
    serial_write("\n");
    return -1;
}

static void h08_ether_header(uint8_t *f, const uint8_t dst_mac[6],
                            uint16_t ethertype)
{
    for (uint32_t i = 0; i < 6U; i++) {
        f[i] = dst_mac[i];
        f[6U + i] = rtl8139_in8(
            (uint16_t)(rtl8139_io_base + i));
    }
    h08_put16(f + 12U, ethertype);
}

/* Return entire Ethernet frame length, 0 on timeout, -1 on malformed RX. */
static int h08_rx_poll(void)
{
    uint16_t io = rtl8139_io_base;
    for (uint32_t n = 0; n < H08_POLL_LIMIT; n++) {
        if (rtl8139_in8((uint16_t)(io + 0x37U)) & 1U)
            continue;

        uint32_t off = h08_rx_offset;
        if (off >= 8192U) return -1;
        volatile uint8_t *p = &rtl_rx_buffer[off];
        uint16_t status = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        if (!(status & 1U) || length < 4U ||
            length > H08_RX_MAX_FRAME + 4U ||
            off + 4U + length > sizeof(rtl_rx_buffer))
            return -1;
        uint32_t frame_size = (uint32_t)length - 4U;
        for (uint32_t i = 0; i < frame_size; i++)
            h08_rx_frame[i] = p[4U + i];

        off = (off + 4U + length + 3U) & ~3U;
        off &= 8191U;
        h08_rx_offset = off;
        rtl8139_out16((uint16_t)(io + 0x38U),
                      (uint16_t)(off - 16U));
        rtl8139_out16((uint16_t)(io + 0x3EU), 1U);
        return (int)frame_size;
    }
    return 0;
}

static int h08_nic_start(void)
{
    if (rtl8139_io_base == 0) return -1;
    uint16_t io = rtl8139_io_base;
    uintptr_t rxaddr = (uintptr_t)&rtl_rx_buffer[0];
    if ((rxaddr & 4095U) != 0U ||
        rxaddr >= (uintptr_t)(0x08000000U - sizeof(rtl_rx_buffer)))
        return -1;

    /* Start a fresh RX ring, as previous 0.7A lab does. */
    rtl8139_out8((uint16_t)(io + 0x37U), 0x10U);
    for (uint32_t n = 0; n < 2000000U; n++) {
        if (!(rtl8139_in8((uint16_t)(io + 0x37U)) & 0x10U))
            break;
    }
    if (rtl8139_in8((uint16_t)(io + 0x37U)) & 0x10U)
        return -1;

    for (uint32_t i = 0; i < sizeof(rtl_rx_buffer); i++)
        rtl_rx_buffer[i] = 0;
    for (uint32_t i = 0; i < 4U; i++) h08_slot_used[i] = 0U;
    h08_tx_slot = 0U;
    h08_rx_offset = 0U;
    h08_ip_id = 0x18A0U;

    rtl8139_out16((uint16_t)(io + 0x3CU), 0);
    rtl8139_out16((uint16_t)(io + 0x3EU), 0xFFFFU);
    rtl8139_out32((uint16_t)(io + 0x30U), (uint32_t)rxaddr);
    rtl8139_out16((uint16_t)(io + 0x38U), 0xFFF0U);
    rtl8139_out32((uint16_t)(io + 0x44U), 0x0000008AU);
    rtl8139_out8((uint16_t)(io + 0x37U), 0x0CU);
    return ((rtl8139_in8((uint16_t)(io + 0x37U)) & 0x0CU) == 0x0CU)
           ? 0 : -1;
}

static int h08_arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6])
{
    uint8_t *f = h08_tx_prepare();
    if (!f) return -1;
    for (uint32_t i = 0; i < 6U; i++) {
        f[i] = 0xFFU;
        f[6U + i] = rtl8139_in8((uint16_t)(rtl8139_io_base + i));
        f[22U + i] = f[6U + i];
    }
    h08_put16(f + 12, 0x0806U);
    h08_put16(f + 14, 1U);
    h08_put16(f + 16, 0x0800U);
    f[18] = 6U; f[19] = 4U;
    h08_put16(f + 20, 1U);
    for (uint32_t i = 0; i < 4U; i++) {
        f[28U + i] = h08_my_ip[i];
        f[38U + i] = target_ip[i];
    }
    if (h08_tx_submit(60U) != 0) return -1;

    for (uint32_t n = 0; n < 24U; n++) {
        int len = h08_rx_poll();
        if (len < 0) return -1;
        if (len == 0) return -1;
        const uint8_t *r = h08_rx_frame;
        if (len < 42 || h08_get16(r + 12) != 0x0806U ||
            h08_get16(r + 14) != 1U ||
            h08_get16(r + 16) != 0x0800U ||
            r[18] != 6U || r[19] != 4U ||
            h08_get16(r + 20) != 2U ||
            !h08_ip_equal(r + 28, target_ip) ||
            !h08_ip_equal(r + 38, h08_my_ip))
            continue;
        for (uint32_t i = 0; i < 6U; i++) out_mac[i] = r[22U + i];
        return 0;
    }
    return -1;
}

/* Validate IPv4 header and return IP offset, header size and datagram length. */
static int h08_ipv4_view(int frame_length, const uint8_t **ip_out,
                         uint32_t *ihl_out, uint32_t *total_out)
{
    const uint8_t *f = h08_rx_frame;
    if (frame_length < 34 || h08_get16(f + 12) != 0x0800U)
        return -1;
    const uint8_t *ip = f + 14;
    uint32_t ihl = (uint32_t)(ip[0] & 15U) * 4U;
    uint32_t total = h08_get16(ip + 2);
    if ((ip[0] >> 4) != 4U || ihl < 20U || ihl > 60U ||
        (uint32_t)frame_length < 14U + ihl ||
        total < ihl || total > (uint32_t)frame_length - 14U ||
        (h08_get16(ip + 6) & 0x3FFFU) != 0U ||
        h08_finish_sum(h08_sum_bytes(ip, ihl)) != 0U ||
        !h08_ip_equal(ip + 16, h08_my_ip))
        return -1;
    *ip_out = ip;
    *ihl_out = ihl;
    *total_out = total;
    return 0;
}

/* Skip a DNS QNAME / compressed NAME without chasing pointers. */
static int h08_dns_skip_name(const uint8_t *dns, uint32_t size,
                            uint32_t *offset)
{
    uint32_t pos = *offset;
    uint32_t labels = 0U;
    while (pos < size && labels++ < 128U) {
        uint8_t label = dns[pos];
        if (label == 0U) { *offset = pos + 1U; return 0; }
        if ((label & 0xC0U) == 0xC0U) {
            if (pos + 1U >= size) return -1;
            *offset = pos + 2U;
            return 0;
        }
        if ((label & 0xC0U) != 0U || label > 63U ||
            pos + 1U + label > size) return -1;
        pos += 1U + label;
    }
    return -1;
}

/* Receive QEMU DNS A response for the exact transaction ID. */
static int h08_dns_get_a(uint8_t out_ip[4])
{
    uint8_t dnsquery[256];
    int dnslen = net_dns_build_query(
        dnsquery, sizeof(dnsquery), h08_host, 0x4D80U);
    if (dnslen <= 0) return -1;

    uint8_t *f = h08_tx_prepare();
    if (!f) return -1;
    h08_ether_header(f, h08_dns_mac, 0x0800U);
    int iplen = net_udp_build_ipv4(
        f + 14U, H08_TX_MAX_FRAME - 14U,
        h08_my_ip, h08_dns_ip, 49153U, 53U,
        dnsquery, (uint16_t)dnslen);
    if (iplen <= 0) return -1;
    if (h08_tx_submit(14U + (uint32_t)iplen) != 0) return -1;
    serial_write("HTTP08B: DNS QUERY "); serial_write(h08_host); serial_write(" ENVIADA\n");

    for (uint32_t tries = 0; tries < 48U; tries++) {
        int len = h08_rx_poll();
        if (len <= 0) return -1;
        const uint8_t *ip;
        uint32_t ihl, iptotal;
        if (h08_ipv4_view(len, &ip, &ihl, &iptotal) != 0 ||
            ip[9] != 17U || !h08_ip_equal(ip + 12, h08_dns_ip) ||
            iptotal < ihl + 8U) continue;
        const uint8_t *udp = ip + ihl;
        uint32_t udpavail = iptotal - ihl;
        uint32_t udplen = h08_get16(udp + 4);
        if (h08_get16(udp) != 53U ||
            h08_get16(udp + 2) != 49153U ||
            udplen < 8U || udplen > udpavail) continue;
        if (h08_get16(udp + 6) != 0U &&
            h08_transport_sum(ip + 12, ip + 16, 17U,
                              udp, (uint16_t)udplen) != 0U) continue;
        const uint8_t *dns = udp + 8;
        uint32_t dnssize = udplen - 8U;
        if (dnssize < 12U || h08_get16(dns) != 0x4D80U ||
            (dns[2] & 0x80U) == 0U || (dns[3] & 0x0FU) != 0U)
            continue;
        uint32_t qd = h08_get16(dns + 4);
        uint32_t an = h08_get16(dns + 6);
        if (qd != 1U || an == 0U || an > 64U) continue;
        uint32_t off = 12U;
        if (h08_dns_skip_name(dns, dnssize, &off) != 0 ||
            off + 4U > dnssize) continue;
        /* Response to our IN/A question */
        if (h08_get16(dns + off) != 1U ||
            h08_get16(dns + off + 2U) != 1U) continue;
        off += 4U;
        for (uint32_t i = 0; i < an; i++) {
            if (h08_dns_skip_name(dns, dnssize, &off) != 0 ||
                off + 10U > dnssize) break;
            uint16_t typ = h08_get16(dns + off);
            uint16_t cls = h08_get16(dns + off + 2U);
            uint16_t rdlength = h08_get16(dns + off + 8U);
            off += 10U;
            if (off + rdlength > dnssize) break;
            if (typ == 1U && cls == 1U && rdlength == 4U) {
                for (uint32_t j = 0; j < 4U; j++)
                    out_ip[j] = dns[off + j];
                /* Only public, unicast IPv4 addresses are allowed in this test. */
                uint8_t x = out_ip[0], y = out_ip[1];
                if (x == 0U || x == 10U || x == 127U || x >= 224U ||
                    (x == 169U && y == 254U) ||
                    (x == 172U && y >= 16U && y <= 31U) ||
                    (x == 192U && y == 168U))
                    return -1;
                serial_write("HTTP08B: DNS IPV4 PUBLICA = ");
                h08_print_ip(out_ip);
                serial_write("\nHTTP08B: DNS OK\n");
                return 0;
            }
            off += rdlength;
        }
    }
    return -1;
}

static void h08_build_ipv4(uint8_t *ip, uint16_t iptotal,
                          const uint8_t dst[4], uint8_t proto)
{
    ip[0] = 0x45U;
    h08_put16(ip + 2, iptotal);
    h08_put16(ip + 4, h08_ip_id++);
    h08_put16(ip + 6, 0x4000U);
    ip[8] = 64U;
    ip[9] = proto;
    for (uint32_t i = 0; i < 4U; i++) {
        ip[12U + i] = h08_my_ip[i];
        ip[16U + i] = dst[i];
    }
    h08_put16(ip + 10, h08_finish_sum(h08_sum_bytes(ip, 20U)));
}

static int h08_tcp_send(uint8_t flags, uint32_t seq, uint32_t ack,
                        const uint8_t *payload, uint32_t payload_size)
{
    if (payload_size > 1200U || (payload_size && !payload))
        return -1;
    uint8_t *f = h08_tx_prepare();
    if (!f) return -1;
    h08_ether_header(f, h08_gateway_mac, 0x0800U);
    uint8_t *ip = f + 14U;
    uint8_t *tcp = ip + 20U;
    uint16_t tcplen = (uint16_t)(20U + payload_size);
    h08_build_ipv4(ip, (uint16_t)(20U + tcplen), h08_target_ip, 6U);
    h08_put16(tcp, h08_local_port);
    h08_put16(tcp + 2, h08_target_port);
    h08_put32(tcp + 4, seq);
    h08_put32(tcp + 8, ack);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    h08_put16(tcp + 14, 4096U);
    for (uint32_t i = 0; i < payload_size; i++)
        tcp[20U + i] = payload[i];
    h08_put16(tcp + 16, h08_transport_sum(
        h08_my_ip, h08_target_ip, 6U, tcp, tcplen));
    return h08_tx_submit(14U + 20U + tcplen);
}

typedef struct {
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    const uint8_t *payload;
    uint32_t payload_size;
} h08_tcp_info;

static int h08_tcp_parse(int length, h08_tcp_info *t)
{
    const uint8_t *ip;
    uint32_t ihl, iptotal;
    if (h08_ipv4_view(length, &ip, &ihl, &iptotal) != 0 ||
        ip[9] != 6U || !h08_ip_equal(ip + 12, h08_target_ip) ||
        iptotal < ihl + 20U) return -1;
    const uint8_t *tcp = ip + ihl;
    uint32_t tcplen = iptotal - ihl;
    uint32_t thl = (uint32_t)(tcp[12] >> 4) * 4U;
    if (thl < 20U || thl > tcplen ||
        h08_get16(tcp) != h08_target_port ||
        h08_get16(tcp + 2) != h08_local_port ||
        h08_transport_sum(ip + 12, ip + 16, 6U,
                          tcp, (uint16_t)tcplen) != 0U)
        return -1;
    t->seq = h08_get32(tcp + 4);
    t->ack = h08_get32(tcp + 8);
    t->flags = tcp[13];
    t->payload = tcp + thl;
    t->payload_size = tcplen - thl;
    return 0;
}

static int h08_tcp_connect(void)
{
    if (h08_tcp_send(0x02U, h08_iss, 0U, 0, 0U) != 0)
        return -1;
    serial_write("HTTP08A: SYN PUBLICO ENVIADO\n");
    for (uint32_t n = 0; n < 48U; n++) {
        int len = h08_rx_poll();
        if (len <= 0) return -1;
        h08_tcp_info t;
        if (h08_tcp_parse(len, &t) != 0) continue;
        if (t.flags & 0x04U) {
            serial_write("HTTP08A: SERVIDOR RECHAZO TCP (RST)\n");
            return -1;
        }
        if ((t.flags & 0x12U) != 0x12U ||
            t.ack != h08_iss + 1U || t.payload_size != 0U)
            continue;
        h08_send_next = h08_iss + 1U;
        h08_recv_next = t.seq + 1U;
        if (h08_tcp_send(0x10U, h08_send_next,
                         h08_recv_next, 0, 0U) != 0)
            return -1;
        serial_write("HTTP08A: TCP PUBLICO CONECTADO\n");
        return 0;
    }
    return -1;
}

static int h08_http_valid_status(const uint8_t *p, uint32_t n)
{
    if (n < 13U) return 0;
    if (p[0] != 'H' || p[1] != 'T' || p[2] != 'T' || p[3] != 'P' ||
        p[4] != '/' || p[5] != '1' || p[6] != '.' ||
        (p[7] != '0' && p[7] != '1') || p[8] != ' ' ||
        p[9] < '1' || p[9] > '5' ||
        p[10] < '0' || p[10] > '9' ||
        p[11] < '0' || p[11] > '9') return 0;
    /* Status line must be terminated by CRLF before we trust it. */
    for (uint32_t i = 12U; i + 1U < n && i < 100U; i++)
        if (p[i] == '\r' && p[i + 1U] == '\n') return 1;
    return 0;
}

static void h08_print_status(const uint8_t *p, uint32_t n)
{
    char line[101];
    uint32_t i = 0U;
    while (i < n && i < 100U && p[i] != '\r' && p[i] != '\n') {
        uint8_t c = p[i];
        line[i] = (c >= 32U && c < 127U) ? (char)c : '.';
        i++;
    }
    line[i] = 0;
    serial_write("HTTP08C: SERVIDOR RESPONDIO: ");
    serial_write(line);
    serial_write("\n");
}

/* ================================================================
 * MiOS HTTP 0.8D/0.8E: file body + runtime URL selection.
 * HTTPS demo uses an explicit, certificate-validating WSL relay.
 * IMPORTANT: THIS DOES NOT IMPLEMENT TLS IN MiOS.
 * ================================================================ */

static uint8_t h08_tolower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

static int h08_ci_begins(const char *str, const char *prefix)
{
    while (*prefix) {
        if (!*str || h08_tolower((uint8_t)*str) !=
                     h08_tolower((uint8_t)*prefix)) return 0;
        str++; prefix++;
    }
    return 1;
}

static int h08_ci_name(const uint8_t *p, uint32_t n, const char *lit)
{
    uint32_t i = 0U;
    while (lit[i]) {
        if (i >= n || h08_tolower(p[i]) != h08_tolower((uint8_t)lit[i]))
            return 0;
        i++;
    }
    return i == n;
}

static int h08_valid_filename(const char *s)
{
    uint32_t n = 0U;
    while (s[n] && n < 32U) {
        uint8_t c = (uint8_t)s[n];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) return 0;
        n++;
    }
    return n > 0U && n < 32U && s[n] == 0 &&
           s[0] != '.';
}

static uint32_t h08_hash_name(const char *s)
{
    uint32_t hash = 2166136261U;
    while (*s) {
        hash ^= (uint8_t)*s++;
        hash *= 16777619U;
    }
    return hash;
}

static void h08_make_filename(const char *url)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t hash = h08_hash_name(url);
    const char *start = "WEB_";
    for (uint32_t i = 0U; i < 4U; i++) h08_filename[i] = start[i];
    for (uint32_t i = 0U; i < 8U; i++)
        h08_filename[4U + i] = hex[(hash >> (28U - 4U * i)) & 15U];
    h08_filename[12] = '.';
    h08_filename[13] = 'B';
    h08_filename[14] = 'I';
    h08_filename[15] = 'N';
    h08_filename[16] = 0;
}

/* Called in the GUI only to store a small request; no network I/O. */
static int h08_set_url_from_gui(const char *command)
{
    if (!h08_ci_begins(command, "DOWNLOAD ")) return -1;
    const char *url = command + 9;
    int bridge = 0;
    uint32_t scheme_length = 0U;
    if (h08_ci_begins(url, "HTTP://")) {
        scheme_length = 7U;
    } else if (h08_ci_begins(url, "HTTPS://")) {
        scheme_length = 8U;
        bridge = 1;
    } else if (h08_ci_begins(url, "HTTP ")) {
        /* MiOS GUI keyboard lacks ':'; allow scheme followed by space. */
        scheme_length = 5U;
    } else if (h08_ci_begins(url, "HTTPS ")) {
        scheme_length = 6U;
        bridge = 1;
    } else return -1;

    const char *p = url + scheme_length;
    char new_host[sizeof(h08_host)];
    char new_path[sizeof(h08_path)];
    char new_url[sizeof(h08_url)];
    char new_filename[sizeof(h08_filename)];
    uint32_t hn = 0U, pn = 0U, fn = 0U;

    while (*p && *p != '/' && *p != '?' && *p != ' ') {
        uint8_t c = h08_tolower((uint8_t)*p++);
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return -1;
        if (hn >= sizeof(new_host) - 1U) return -1;
        new_host[hn++] = (char)c;
    }
    if (hn < 3U || new_host[0] == '.' || new_host[hn - 1U] == '.' ||
        new_host[0] == '-' || new_host[hn - 1U] == '-') return -1;
    int has_dot = 0;
    for (uint32_t i = 0U; i < hn; i++)
        if (new_host[i] == '.') has_dot = 1;
    if (!has_dot) return -1;
    new_host[hn] = 0;

    if (*p == '/') {
        while (*p && *p != ' ') {
            uint8_t c = (uint8_t)*p++;
            if (c <= 32U || c >= 127U || c == '#' || c == '\\')
                return -1;
            if (pn >= sizeof(new_path) - 1U) return -1;
            new_path[pn++] = (char)c;
        }
    } else if (*p == '?') {
        new_path[pn++] = '/';
        while (*p && *p != ' ') {
            uint8_t c = (uint8_t)*p++;
            if (c <= 32U || c >= 127U || c == '#' || c == '\\')
                return -1;
            if (pn >= sizeof(new_path) - 1U) return -1;
            new_path[pn++] = (char)c;
        }
    } else {
        new_path[pn++] = '/';
    }
    new_path[pn] = 0;

    while (*p == ' ') p++;
    if (*p) {
        if (!h08_ci_begins(p, "AS ")) return -1;
        p += 3;
        while (*p && *p != ' ') {
            if (fn >= sizeof(new_filename) - 1U) return -1;
            new_filename[fn++] = *p++;
        }
        new_filename[fn] = 0;
        while (*p == ' ') p++;
        if (*p || !h08_valid_filename(new_filename)) return -1;
    }

    const char *scheme = bridge ? "https://" : "http://";
    uint32_t u = 0U;
    for (uint32_t i = 0U; scheme[i]; i++) {
        if (u >= sizeof(new_url) - 1U) return -1;
        new_url[u++] = scheme[i];
    }
    for (uint32_t i = 0U; i < hn; i++) {
        if (u >= sizeof(new_url) - 1U) return -1;
        new_url[u++] = new_host[i];
    }
    for (uint32_t i = 0U; i < pn; i++) {
        if (u >= sizeof(new_url) - 1U) return -1;
        new_url[u++] = new_path[i];
    }
    new_url[u] = 0;

    for (uint32_t i = 0U; i <= hn; i++) h08_host[i] = new_host[i];
    for (uint32_t i = 0U; i <= pn; i++) h08_path[i] = new_path[i];
    for (uint32_t i = 0U; i <= u; i++) h08_url[i] = new_url[i];
    h08_bridge_mode = bridge;
    h08_target_port = bridge ? 8081U : 80U;
    h08_make_filename(h08_url);
    if (fn) {
        for (uint32_t i = 0U; i <= fn; i++)
            h08_filename[i] = new_filename[i];
    }
    h08_url_configured = 1;
    return 0;
}

static int h08_is_gui_url_command(const char *line)
{
    return h08_ci_begins(line, "DOWNLOAD HTTP://") ||
           h08_ci_begins(line, "DOWNLOAD HTTPS://") ||
           h08_ci_begins(line, "DOWNLOAD HTTP ") ||
           h08_ci_begins(line, "DOWNLOAD HTTPS ");
}

/* Accessors for the earlier GUI interpreter in kernel.c. */
static const char *h08_get_filename(void) { return h08_filename; }
static int h08_using_bridge(void) { return h08_bridge_mode; }

static int h08_request_append(uint8_t *buffer, uint32_t cap,
                              uint32_t *pos, const char *text)
{
    while (*text) {
        if (*pos >= cap) return -1;
        buffer[(*pos)++] = (uint8_t)*text++;
    }
    return 0;
}

static int h08_build_request(uint8_t *out, uint32_t capacity)
{
    uint32_t pos = 0U;
#define H08_APPEND(x) do { if (h08_request_append(out, capacity, &pos, (x)) != 0) return -1; } while (0)
    if (h08_bridge_mode) {
        H08_APPEND("GET /fetch HTTP/1.0\r\n");
        H08_APPEND("Host: 10.0.2.2:8081\r\n");
        H08_APPEND("X-MiOS-URL: ");
        H08_APPEND(h08_url);
        H08_APPEND("\r\n");
    } else {
        H08_APPEND("GET ");
        H08_APPEND(h08_path);
        H08_APPEND(" HTTP/1.0\r\nHost: ");
        H08_APPEND(h08_host);
        H08_APPEND("\r\n");
    }
    H08_APPEND("User-Agent: MiOS-HTTP08\r\n");
    H08_APPEND("Accept-Encoding: identity\r\n");
    H08_APPEND("Connection: close\r\n\r\n");
#undef H08_APPEND
    return (int)pos;
}

static int h08_header_end(const uint8_t *p, uint32_t n,
                          uint32_t *out)
{
    for (uint32_t i = 0U; i + 3U < n; i++) {
        if (p[i] == '\r' && p[i + 1U] == '\n' &&
            p[i + 2U] == '\r' && p[i + 3U] == '\n') {
            *out = i + 4U;
            return 0;
        }
    }
    return -1;
}

static int h08_hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    c = h08_tolower(c);
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return -1;
}

/* Decode RFC-style HTTP/1.1 chunked framing with strict bounds. */
static int h08_decode_chunked(const uint8_t *p, uint32_t n)
{
    uint32_t at = 0U, used = 0U;
    while (at < n) {
        uint32_t chunk = 0U, digits = 0U;
        while (at < n && p[at] != '\r' && p[at] != ';') {
            int digit = h08_hex_digit(p[at++]);
            if (digit < 0 || chunk > (BINFS_MAX_FILE_SIZE - (uint32_t)digit) / 16U)
                return -1;
            chunk = chunk * 16U + (uint32_t)digit;
            digits++;
        }
        if (digits == 0U) return -1;
        while (at < n && p[at] != '\r') at++; /* extension */
        if (at + 1U >= n || p[at] != '\r' || p[at + 1U] != '\n')
            return -1;
        at += 2U;
        if (chunk == 0U) {
            /* Trailers, including the empty trailer line. */
            while (at + 1U < n) {
                uint32_t start = at;
                while (at + 1U < n && !(p[at] == '\r' && p[at + 1U] == '\n'))
                    at++;
                if (at + 1U >= n) return -1;
                uint32_t line_len = at - start;
                at += 2U;
                if (line_len == 0U) return at == n ? (int)used : -1;
            }
            return -1;
        }
        if (chunk > BINFS_MAX_FILE_SIZE - used ||
            at + chunk + 2U > n) return -1;
        for (uint32_t i = 0; i < chunk; i++)
            h08_body[used + i] = p[at + i];
        used += chunk;
        at += chunk;
        if (p[at] != '\r' || p[at + 1U] != '\n') return -1;
        at += 2U;
    }
    return -1;
}

/* Decode only a complete HTTP response after TCP FIN was received. */
static int h08_extract_body(uint32_t captured)
{
    uint32_t body_off;
    if (!h08_http_valid_status(h08_capture, captured) ||
        h08_header_end(h08_capture, captured, &body_off) != 0 ||
        body_off > 3072U) return -1;
    if (h08_capture[9] != '2' || h08_capture[10] != '0' ||
        h08_capture[11] != '0') {
        serial_write("HTTP08D: HTTP NO ES 200, NO GUARDAR\n");
        return -1;
    }
    int has_length = 0, has_chunked = 0;
    uint32_t content_length = 0U;
    uint32_t line = 0U;
    while (line + 1U < body_off &&
           !(h08_capture[line] == '\r' && h08_capture[line + 1U] == '\n'))
        line++;
    if (line + 1U >= body_off) return -1;
    line += 2U;
    while (line + 1U < body_off) {
        uint32_t end = line;
        while (end + 1U < body_off &&
               !(h08_capture[end] == '\r' && h08_capture[end + 1U] == '\n'))
            end++;
        if (end + 1U >= body_off) return -1;
        if (end == line) break;
        uint32_t sep = line;
        while (sep < end && h08_capture[sep] != ':') sep++;
        if (sep == end) return -1;
        uint32_t val = sep + 1U;
        while (val < end && (h08_capture[val] == ' ' || h08_capture[val] == '\t')) val++;
        if (h08_ci_name(h08_capture + line, sep - line, "Content-Length")) {
            if (has_length || val == end) return -1;
            has_length = 1;
            while (val < end && h08_capture[val] >= '0' && h08_capture[val] <= '9') {
                uint32_t digit = (uint32_t)(h08_capture[val++] - '0');
                if (content_length > (BINFS_MAX_FILE_SIZE - digit) / 10U)
                    return -1;
                content_length = content_length * 10U + digit;
            }
            while (val < end && (h08_capture[val] == ' ' || h08_capture[val] == '\t')) val++;
            if (val != end) return -1;
        } else if (h08_ci_name(h08_capture + line, sep - line, "Transfer-Encoding")) {
            /* This minimal client supports only a single 'chunked' transfer coding. */
            if (has_chunked || end - val != 7U ||
                !h08_ci_name(h08_capture + val, end - val, "chunked"))
                return -1;
            has_chunked = 1;
        }
        line = end + 2U;
    }
    if (has_chunked && has_length) return -1;
    uint32_t payload_len = captured - body_off;
    if (has_chunked) {
        int n = h08_decode_chunked(h08_capture + body_off, payload_len);
        if (n <= 0) return -1;
        h08_body_size = (uint32_t)n;
    } else {
        if (payload_len == 0U || payload_len > BINFS_MAX_FILE_SIZE)
            return -1;
        if (has_length && content_length != payload_len)
            return -1;
        h08_body_size = payload_len;
        for (uint32_t i = 0U; i < payload_len; i++)
            h08_body[i] = h08_capture[body_off + i];
    }
    serial_write("HTTP08D: CUERPO RECIBIDO BYTES = ");
    serial_write_hex32(h08_body_size);
    serial_write("\n");
    return 0;
}

static int h08_binfs_save(void)
{
    if (h08_body_size == 0U || h08_body_size > BINFS_MAX_FILE_SIZE ||
        !h08_valid_filename(h08_filename)) return -1;
    int count = binfs_count();
    if (count < 0 || count > 4) {
        serial_write("HTTP08D: CATALOGO BINFS INVALIDO\n");
        return -1;
    }
    int exists = 0;
    /* Query physical slot indices; deleted entries are not readable. */
    for (uint32_t i = 0U; i < 4U; i++) {
        BinfsEntry entry;
        if (binfs_get_entry(i, &entry) != 0) continue;
        uint32_t j = 0U;
        while (j < 32U && entry.name[j] && h08_filename[j] &&
               entry.name[j] == h08_filename[j]) j++;
        if (j < 32U && entry.name[j] == 0 && h08_filename[j] == 0)
            exists = 1;
    }
    if (!exists) {
        if (binfs_create_from_buffer(h08_filename, h08_body, h08_body_size) != 0) {
            serial_write("HTTP08D: NO SE PUDO CREAR EN BINFS\n");
            return -1;
        }
        serial_write("HTTP08D: ARCHIVO BINFS CREADO\n");
    } else {
        serial_write("HTTP08D: ARCHIVO EXISTENTE; VERIFICANDO\n");
    }
    uint32_t read_size = 0U;
    if (binfs_read(h08_filename, h08_disk_verify,
                   sizeof(h08_disk_verify), &read_size) != 0 ||
        read_size != h08_body_size) return -1;
    for (uint32_t i = 0U; i < read_size; i++)
        if (h08_disk_verify[i] != h08_body[i]) {
            serial_write("HTTP08D: EXISTENTE DISTINTO: NO SOBRESCRIBIR\n");
            return -1;
        }
    serial_write("HTTP08D: ARCHIVO = ");
    serial_write(h08_filename);
    serial_write("\nHTTP08D: BINFS READBACK OK\n");
    return 0;
}

static int h08_http_get_public(void)
{
    static uint8_t request[1024];
    int request_len = h08_build_request(request, sizeof(request));
    if (request_len <= 0) return -1;
    if (h08_tcp_send(0x18U, h08_send_next, h08_recv_next,
                     request, (uint32_t)request_len) != 0)
        return -1;
    h08_send_next += (uint32_t)request_len;
    serial_write("HTTP08C: GET PUBLICO ENVIADO\n");

    uint32_t capture_size = 0U;
    uint32_t total_received = 0U;
    int status_ok = 0;

    /* The request forces Connection: close. Completion requires TCP FIN. */
    for (uint32_t n = 0; n < 512U; n++) {
        int len = h08_rx_poll();
        if (len <= 0) {
            serial_write("HTTP08C: SIN FIN O TIMEOUT, RESPUESTA INCOMPLETA\n");
            return -1;
        }
        h08_tcp_info t;
        if (h08_tcp_parse(len, &t) != 0) continue;
        if (t.flags & 0x04U) {
            serial_write("HTTP08C: TCP RST RECIBIDO\n");
            return -1;
        }
        if ((t.flags & 0x10U) == 0U || t.ack != h08_send_next)
            continue;

        uint32_t trim = 0U;
        int32_t diff = (int32_t)(t.seq - h08_recv_next);
        if (diff > 0) {
            if (h08_tcp_send(0x10U, h08_send_next,
                             h08_recv_next, 0, 0U) != 0) return -1;
            continue;
        }
        if (diff < 0) trim = h08_recv_next - t.seq;
        if (trim > t.payload_size) trim = t.payload_size;
        uint32_t fresh = t.payload_size - trim;
        if (fresh) {
            if (fresh > 32768U - total_received ||
                fresh > H08_CAPTURE_MAX - capture_size) {
                serial_write("HTTP08D: RESPUESTA SUPERA BUFFER HTTP\n");
                return -1;
            }
            for (uint32_t i = 0U; i < fresh; i++)
                h08_capture[capture_size + i] = t.payload[trim + i];
            capture_size += fresh;
            total_received += fresh;
            h08_recv_next += fresh;
        }
        uint32_t fin_position = t.seq + t.payload_size;
        int fresh_fin = (t.flags & 0x01U) &&
                        (fin_position == h08_recv_next);
        if (fresh_fin) h08_recv_next++;
        if (fresh || t.payload_size || fresh_fin) {
            if (h08_tcp_send(0x10U, h08_send_next,
                             h08_recv_next, 0, 0U) != 0) return -1;
        }
        if (!status_ok && h08_http_valid_status(h08_capture, capture_size)) {
            status_ok = 1;
            h08_print_status(h08_capture, capture_size);
        }
        if (fresh_fin) {
            serial_write("HTTP08C: FIN REMOTO RECIBIDO\n");
            if (h08_tcp_send(0x11U, h08_send_next,
                             h08_recv_next, 0, 0U) != 0) return -1;
            h08_send_next++;
            if (!status_ok) return -1;
            if (h08_extract_body(capture_size) != 0) {
                serial_write("HTTP08D: CUERPO HTTP INVALIDO/O MUY GRANDE\n");
                return -1;
            }
            serial_write("HTTP08C: HTTP PUBLICO RECIBIDO\n");
            return 0;
        }
    }
    return -1;
}

/* Public entrypoint, one HTTP request per boot. */
static int mios_http08_public_test(void)
{
    if (h08_test_used) {
        serial_write("HTTP08: SOLO UNA PRUEBA POR ARRANQUE\n");
        return -1;
    }
    h08_test_used = 1;
    if (!h08_url_configured &&
        h08_set_url_from_gui("DOWNLOAD HTTP://EXAMPLE.COM/") != 0)
        return -1;
    h08_body_size = 0U;
    serial_write("\n=== MiOS HTTP 0.8D/E: INTERNET PUBLICO ===\n");
    serial_write("HTTP08: URL = ");
    serial_write(h08_url);
    serial_write("\n");
    if (h08_bridge_mode)
        serial_write("HTTPS: PUENTE LOCAL WSL; TLS NO ES NATIVO DE MiOS\n");

    if (h08_nic_start() != 0) {
        serial_write("HTTP08: ERROR INICIANDO DMA RTL8139\n");
        return -1;
    }
    if (h08_arp_resolve(h08_gateway_ip, h08_gateway_mac) != 0) {
        serial_write("HTTP08A: ERROR ARP GATEWAY\n");
        return -1;
    }
    serial_write("HTTP08A: GATEWAY MAC RESUELTA\n");

    if (h08_bridge_mode) {
        for (uint32_t i = 0U; i < 4U; i++)
            h08_target_ip[i] = h08_gateway_ip[i];
        h08_target_port = 8081U;
        serial_write("HTTPS: DESTINO PUENTE 10.0.2.2:8081\n");
    } else {
        if (h08_arp_resolve(h08_dns_ip, h08_dns_mac) != 0) {
            serial_write("HTTP08B: ERROR ARP DNS\n");
            return -1;
        }
        h08_target_port = 80U;
        if (h08_dns_get_a(h08_target_ip) != 0) {
            serial_write("HTTP08B: ERROR DNS PUBLICO\n");
            return -1;
        }
    }

    uint32_t tsc_lo, tsc_hi;
    __asm__ volatile("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
    h08_iss = (tsc_lo ^ (tsc_hi << 7)) + 0x12345678U;
    h08_local_port = (uint16_t)(49154U + (tsc_lo & 511U));
    h08_send_next = 0U;
    h08_recv_next = 0U;

    if (h08_tcp_connect() != 0) {
        serial_write("HTTP08A: TCP PUBLICO FALLO\n");
        return -1;
    }
    if (h08_http_get_public() != 0) {
        serial_write("HTTP08C: HTTP PUBLICO FALLO\n");
        return -1;
    }
    serial_write("HTTP08C: PRIMER GET PUBLICO COMPLETADO!\n");
    if (h08_binfs_save() != 0) {
        serial_write("HTTP08D: ERROR GUARDANDO EN BINFS\n");
        return -1;
    }
    serial_write("HTTP08D: DESCARGA PUBLICA GUARDADA!\n");
    serial_write("HTTP08E: URL VARIABLE COMPLETADA!\n");
    if (h08_bridge_mode)
        serial_write("HTTPS BRIDGE: DESCARGA HTTPS CON TLS EN UBUNTU OK\n");
    return 0;
}
