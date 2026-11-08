/*
 * Experimental native TLS 4.3-4.5 glue for Mbed TLS 3.6.x LTS.
 * NOT enabled by default. Requires the authentic Mbed TLS 3.6.7 source,
 * a freestanding i386 build, libc/platform shims, UTC time conversion,
 * a trusted CA bundle and secure entropy. See README.
 *
 * Activation guard: MIOS_TLS4_NATIVE_MBEDTLS36=1.
 * Do not claim native TLS success until TLS NATIVE HANDSHAKE verifies a peer.
 */
#if defined(MIOS_TLS4_NATIVE_MBEDTLS36) && MIOS_TLS4_NATIVE_MBEDTLS36
#include <stddef.h>
#include <time.h>
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"
#include "mios_tls4_roots.inc"

#if !defined(MBEDTLS_HAVE_TIME_DATE) || !defined(MBEDTLS_PLATFORM_GMTIME_R_ALT)
#error "TLS4.5 requires certificate DATE verification (MBEDTLS_HAVE_TIME_DATE + GMTIME_R_ALT)"
#endif
#if !defined(MBEDTLS_PLATFORM_TIME_ALT) || !defined(MBEDTLS_PLATFORM_MEMORY)
#error "TLS4.2 requires UTC clock and custom calloc/free support"
#endif
#if !defined(MBEDTLS_SSL_KEEP_PEER_CERTIFICATE)
#error "TLS4.5 requires retained peer cert to verify identity"
#endif

/* 4.2: Convert epoch seconds to UTC calendar time, with no libc time(). */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt,
                                     struct tm *dst)
{
    if (!tt || !dst || *tt < 0) return 0;
    uint32_t epoch = (uint32_t)*tt;
    uint32_t days = epoch / 86400U;
    uint32_t remainder = epoch % 86400U;
    uint32_t year = 1970U;
    while (year < 2038U) {
        uint32_t yd = tls4_leap(year) ? 366U : 365U;
        if (days < yd) break;
        days -= yd;
        year++;
    }
    if (year >= 2038U) return 0;
    uint32_t yday = days;
    uint32_t month = 1U;
    while (month <= 12U) {
        uint32_t md = tls4_days_in_month(year,month);
        if (days < md) break;
        days -= md;
        month++;
    }
    if (month > 12U) return 0;
    dst->tm_year = (int)year - 1900;
    dst->tm_mon = (int)month - 1;
    dst->tm_mday = (int)days + 1;
    dst->tm_hour = (int)(remainder / 3600U);
    dst->tm_min = (int)((remainder % 3600U) / 60U);
    dst->tm_sec = (int)(remainder % 60U);
    dst->tm_yday = (int)yday;
    dst->tm_wday = (int)((4U + epoch / 86400U) % 7U);
    dst->tm_isdst = 0;
    return dst;
}

static mbedtls_time_t tls4_mbed_time(mbedtls_time_t *t)
{
    uint32_t utc = 0U;
    if (tls4_clock_epoch(&utc) != 0) return (mbedtls_time_t)-1;
    if (t) *t = (mbedtls_time_t)utc;
    return (mbedtls_time_t)utc;
}
static void *tls4_mbed_calloc(size_t n, size_t size)
{
    if (n > 0xFFFFFFFFU || size > 0xFFFFFFFFU) return 0;
    return tls4_calloc((uint32_t)n, (uint32_t)size);
}
static void tls4_mbed_free(void *p) { tls4_free(p); }

/* Strong source registration is only justified when the emulator's
 * RDRAND generator has been assessed and is trusted for the test. */
static int tls4_mbed_entropy(void *unused, unsigned char *out,
                             size_t n, size_t *olen)
{
    (void)unused;
    if (!olen || !out || n > 1024U) return -1;
    *olen = 0U;
    if (tls4_entropy_bytes(out, (uint32_t)n) != 0) return -1;
    *olen = n;
    return 0;
}
static int tls4_mbed_send(void *context, const unsigned char *buf, size_t n)
{
    (void)context;
    if (n > 65536U) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int rc = tls4_tcp_send_bytes(buf, (uint32_t)n);
    return rc >= 0 ? rc : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int tls4_mbed_recv(void *context, unsigned char *buf, size_t n)
{
    (void)context;
    if (n > 65536U) n = 65536U;
    int rc = tls4_tcp_recv_bytes(buf, (uint32_t)n);
    if (rc == -2) return MBEDTLS_ERR_SSL_WANT_READ;
    if (rc == -1) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return rc;
}

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    int active;
} MiosTls4Session;
static MiosTls4Session tls4_native;

static void tls4_native_cleanup(void)
{
    mbedtls_ssl_free(&tls4_native.ssl);
    mbedtls_ssl_config_free(&tls4_native.conf);
    mbedtls_x509_crt_free(&tls4_native.ca);
    mbedtls_ctr_drbg_free(&tls4_native.drbg);
    mbedtls_entropy_free(&tls4_native.entropy);
    tls4_native.active = 0;
    tls4_tcp_close();
}

/* Stage 4.4 + 4.5: completes handshake ONLY with strict certificate validation.
 * This is not linked in the default package; see README security gates. */
static int tls4_native_handshake_strict(const char *host)
{
    if (!tls4_host_ok(host) || tls4_native.active) return -1;
    if (tls4_platform_check() != 0) return -1;
    tls4_heap_init();
    if (mbedtls_platform_set_calloc_free(tls4_mbed_calloc,
                                         tls4_mbed_free) != 0) return -1;
    if (mbedtls_platform_set_time(tls4_mbed_time) != 0) return -1;
    mbedtls_ssl_init(&tls4_native.ssl);
    mbedtls_ssl_config_init(&tls4_native.conf);
    mbedtls_x509_crt_init(&tls4_native.ca);
    mbedtls_ctr_drbg_init(&tls4_native.drbg);
    mbedtls_entropy_init(&tls4_native.entropy);
    tls4_native.active = 1;

    int rc = mbedtls_entropy_add_source(
        &tls4_native.entropy, tls4_mbed_entropy, 0,
        48U, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (rc != 0) goto fail;
    const unsigned char personalization[] = "MiOS-4.5-TLS-Client";
    rc = mbedtls_ctr_drbg_seed(
        &tls4_native.drbg, mbedtls_entropy_func, &tls4_native.entropy,
        personalization, sizeof(personalization)-1U);
    if (rc != 0) goto fail;

    /* Entire trusted root set generated from a reviewed system trust store. */
    rc = mbedtls_x509_crt_parse(
        &tls4_native.ca, mios_tls4_ca_pem, sizeof(mios_tls4_ca_pem));
    if (rc != 0) goto fail;
    rc = mbedtls_ssl_config_defaults(&tls4_native.conf,
          MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
          MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) goto fail;
    mbedtls_ssl_conf_authmode(&tls4_native.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls4_native.conf, &tls4_native.ca, 0);
    mbedtls_ssl_conf_rng(&tls4_native.conf, mbedtls_ctr_drbg_random,
                         &tls4_native.drbg);
    rc = mbedtls_ssl_setup(&tls4_native.ssl, &tls4_native.conf);
    if (rc != 0) goto fail;
    rc = mbedtls_ssl_set_hostname(&tls4_native.ssl, host); /* SNI + hostname */
    if (rc != 0) goto fail;
    if (tls4_tcp_connect_host(host, 443U) != 0) goto fail;
    mbedtls_ssl_set_bio(&tls4_native.ssl, 0,
                        tls4_mbed_send, tls4_mbed_recv, 0);

    uint32_t read_timeouts=0U;
    for (uint32_t retries=0U; retries<128U; retries++) {
        rc = mbedtls_ssl_handshake(&tls4_native.ssl);
        if (rc == 0) break;
        if (rc != MBEDTLS_ERR_SSL_WANT_READ &&
            rc != MBEDTLS_ERR_SSL_WANT_WRITE) goto fail;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ && ++read_timeouts > 6U)
            goto fail; /* no unbounded spin when peer is silent */
    }
    if (rc != 0) goto fail;
    if (mbedtls_ssl_get_verify_result(&tls4_native.ssl) != 0U ||
        mbedtls_ssl_get_peer_cert(&tls4_native.ssl) == 0) goto fail;
    serial_write("TLS4.4: HANDSHAKE TLS NATIVO COMPLETO\n");
    serial_write("TLS4.5: CADENA CERTIFICADOS Y HOSTNAME OK\n");
    return 0;
fail:
    serial_write("TLS4.4/4.5: ERROR TLS O CERTIFICADO, CONEXION RECHAZADA\n");
    tls4_native_cleanup();
    return -1;
}
#else
/* Fail closed: native TLS is deliberately unavailable without the library. */
#endif
