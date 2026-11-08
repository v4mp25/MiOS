/* MiOS TLS Phase 4.2: fail-closed platform prerequisites.
 * Real hardware entropy (RDRAND) and UTC CMOS clock; NOT a cryptographic
 * security audit. QEMU -cpu max exposes host/emulated RNG depending on setup.
 */
#ifndef MIOS_TLS4_PLATFORM_INC
#define MIOS_TLS4_PLATFORM_INC

static uint8_t tls4_cmos_byte(uint8_t reg)
{
    outb(0x70U, (uint8_t)(reg & 0x7FU));
    return inb(0x71U);
}
static uint8_t tls4_bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10U + (v & 15U)); }
static uint32_t tls4_leap(uint32_t y) { return (y % 4U == 0U && (y % 100U != 0U || y % 400U == 0U)); }
static uint32_t tls4_days_in_month(uint32_t y, uint32_t m)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 0U || m > 12U) return 0U;
    return (uint32_t)days[m-1U] + ((m == 2U) ? tls4_leap(y) : 0U);
}
static int tls4_clock_snapshot(uint8_t *sec, uint8_t *min,
                               uint8_t *hour, uint8_t *day,
                               uint8_t *mon, uint8_t *year,
                               uint8_t *regb)
{
    if (tls4_cmos_byte(0x0AU) & 0x80U) return -1;  /* updating */
    *regb = tls4_cmos_byte(0x0BU);
    *sec = tls4_cmos_byte(0x00U);
    *min = tls4_cmos_byte(0x02U);
    *hour = tls4_cmos_byte(0x04U);
    *day = tls4_cmos_byte(0x07U);
    *mon = tls4_cmos_byte(0x08U);
    *year = tls4_cmos_byte(0x09U);
    return (tls4_cmos_byte(0x0AU) & 0x80U) ? -1 : 0;
}

/* Seconds since 1970-01-01 UTC; returns -1 for invalid/out-of-range dates.
 * Requires QEMU -rtc base=utc; CMOS time is NOT independently authenticated.
 */
static int tls4_clock_epoch(uint32_t *epoch_out)
{
    if (!epoch_out) return -1;
    uint8_t s, mi, h, d, mo, y, b;
    uint8_t ss, mm, hh, dd, mon, yy, bb;
    int stable = 0;
    for (uint32_t tries = 0U; tries < 16U; tries++) {
        if (tls4_clock_snapshot(&s,&mi,&h,&d,&mo,&y,&b) != 0) continue;
        if (tls4_clock_snapshot(&ss,&mm,&hh,&dd,&mon,&yy,&bb) != 0) continue;
        if (s==ss && mi==mm && h==hh && d==dd && mo==mon && y==yy && b==bb) {
            stable = 1; break;
        }
    }
    if (!stable) return -1;
    int pm = (h & 0x80U) != 0;
    h &= 0x7FU;
    if ((b & 0x04U) == 0U) {
        s=tls4_bcd(s); mi=tls4_bcd(mi); h=tls4_bcd(h);
        d=tls4_bcd(d); mo=tls4_bcd(mo); y=tls4_bcd(y);
    }
    if ((b & 0x02U) == 0U) {
        if (h < 1U || h > 12U) return -1;
        if (h == 12U) h = 0U;
        if (pm) h = (uint8_t)(h + 12U);
    }
    uint32_t year = 2000U + y;
    /* Fail closed outside the safe 32-bit time_t range. */
    if (year < 2024U || year > 2037U || mo < 1U || mo > 12U ||
        d < 1U || d > tls4_days_in_month(year,mo) ||
        h > 23U || mi > 59U || s > 59U) return -1;
    uint32_t days = 0U;
    for (uint32_t yy2 = 1970U; yy2 < year; yy2++)
        days += tls4_leap(yy2) ? 366U : 365U;
    for (uint32_t m = 1U; m < mo; m++) days += tls4_days_in_month(year,m);
    days += (uint32_t)d - 1U;
    *epoch_out = ((days * 24U + h) * 60U + mi) * 60U + s;
    return 0;
}

static int tls4_cpu_has_rdrand(void)
{
#if defined(__i386__) || defined(__x86_64__)
    uint32_t a=1U,b=0U,c=0U,d=0U;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d) : "c"(0U));
    return (c & (1U<<30)) != 0U;
#else
    return 0;
#endif
}
static int tls4_rdrand32(uint32_t *out)
{
#if defined(__i386__) || defined(__x86_64__)
    if (!out || !tls4_cpu_has_rdrand()) return -1;
    for (uint32_t tries=0U; tries<16U; tries++) {
        uint8_t ok;
        uint32_t x;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(x), "=qm"(ok) : : "cc");
        if (ok) { *out=x; return 0; }
    }
    return -1;
#else
    (void)out;
    return -1;
#endif
}
/* On QEMU, hardware RNG trust depends on host/hypervisor; laboratory only.
 * RDRAND provides entropy input for a library DRBG, NEVER TLS key bytes directly.
 */
static int tls4_entropy_bytes(uint8_t *out, uint32_t len)
{
    if (!out || !len || len > 1024U || !tls4_cpu_has_rdrand()) return -1;
    uint32_t prev=0U, prev_valid=0U;
    uint32_t at=0U;
    while (at < len) {
        uint32_t x=0U;
        if (tls4_rdrand32(&x)!=0) return -1;
        /* Minimal continuous output test; not a complete entropy assessment. */
        if (prev_valid && x==prev) return -1;
        prev=x; prev_valid=1U;
        for (uint32_t j=0U; j<4U && at<len; j++) {
            out[at++]=(uint8_t)(x >> (j*8U));
        }
    }
    return 0;
}
static int tls4_platform_check(void)
{
    uint32_t t=0U;
    if (tls4_clock_epoch(&t)!=0) {
        serial_write("TLS4.2: RELOJ UTC NO VALIDO\n"); return -1;
    }
    serial_write("TLS4.2: UTC EPOCH="); serial_write_hex32(t); serial_write("\n");
    if (!tls4_cpu_has_rdrand()) {
        serial_write("TLS4.2: RDRAND NO DISPONIBLE\n"); return -1;
    }
    uint8_t nonce[48];
    if (tls4_entropy_bytes(nonce,sizeof(nonce))!=0) {
        serial_write("TLS4.2: ENTROPIA NO DISPONIBLE\n"); return -1;
    }
    for (uint32_t i=0U;i<sizeof(nonce);i++) nonce[i]=0U;
    serial_write("TLS4.2: RDRAND DISPONIBLE (LAB)\n");
    serial_write("TLS4.2: ATENCION RNG/RELOJ AUN REQUIEREN AUDITORIA\n");
    return 0;
}
#endif
