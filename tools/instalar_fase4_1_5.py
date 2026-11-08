#!/usr/bin/env python3
"""Safe local installer, MiOS TLS 4.1/4.2 network test + 4.3-4.5 source.
Only edits kernel/kernel.c AFTER verifying exact anchors; backups first.
It intentionally DOES NOT link native TLS without a reviewed Mbed TLS port.
"""
from pathlib import Path
import re, shutil, datetime, sys
root=Path.cwd()
kernel=root/'kernel/kernel.c'
required=[root/'kernel/net_public_09.inc',root/'kernel/mios_tls4_stage.inc',
          root/'kernel/mios_tls4_stream.inc',root/'kernel/mios_tls4_heap.inc',
          root/'kernel/mios_tls4_platform.inc',root/'kernel/mios_tls4_mbedtls36.inc']
def fail(msg): raise SystemExit('TLS4 INSTALL ERROR: '+msg)
if not kernel.is_file() or any(not p.is_file() for p in required):
    fail('faltan archivos; extrae el ZIP desde ~/MiOS')
s=kernel.read_text(encoding='utf-8')
if 'mios_tls4_stage.inc' in s:
    fail('ya integrado; no duplico el parche')
anchors=['#include "net_public_09.inc"','static void term_execute(void)',
         'else if (term_equals(term_input, "VER"))',
         'static int rtl8139_dma_arp_test(void)']
for a in anchors:
    if s.count(a)!=1: fail('ancla ausente o duplicada '+repr(a))
# 4.1/4.2 adapter AFTER working HTTP 0.9/1.0 module.
s=s.replace('#include "net_public_09.inc"',
            '#include "net_public_09.inc"\n#include "mios_tls4_stage.inc"',1)
# flags before GUI interpreter
s=s.replace('static void term_execute(void)',
            'static volatile int mios_tls4_request = 0; /* 1=CHECK, 2=TCP443, 3=NATIVE */\n'
            'static void term_execute(void)',1)
new_cases='''else if (term_equals(term_input, "TLS CHECK")) {
    mios_tls4_request = 1;
    term_print("TLS: PROBANDO REQUISITOS");
}
else if (term_equals(term_input, "TLS TCP443")) {
    mios_tls4_request = 2;
    term_print("TLS: PROBANDO TCP 443");
}
else if (term_equals(term_input, "TLS NATIVE")) {
    mios_tls4_request = 3;
    term_print("TLS: HANDSHAKE 4.4 / CERT 4.5");
}
'''
s=s.replace('else if (term_equals(term_input, "VER"))',
            new_cases+'else if (term_equals(term_input, "VER"))',1)
# Main loop (after init, not in PS/2 IRQ). Unique anchored to mostrar_prompt.
rx=re.compile(r'(mostrar_prompt\(\);\s*while\s*\(1\)\s*\{)',re.M)
if len(rx.findall(s))!=1: fail('no localizo el bucle principal de entrada')
code='''\n    /* TLS4: service GUI request in main loop, never inside an IRQ. */
    if (mios_tls4_request != 0) {
        int operation = mios_tls4_request;
        mios_tls4_request = 0;
        int tls_rc = -1;
        if (operation == 1) {
            tls_rc = mios_tls4_check();
            term_print(tls_rc == 0 ? "TLS: PLATAFORMA LAB OK" :
                                  "TLS: PLATAFORMA NO LISTA");
        } else if (operation == 2) {
            if (!mios_network_ready) {
                serial_write("TLS4.1: RTL8139 NO DISPONIBLE\\n");
            } else {
                tls_rc = mios_tls4_probe_tcp443();
            }
            term_print(tls_rc == 0 ? "TLS4.1: TCP443 OK (NO TLS)" :
                                  "TLS4.1: ERROR TCP443");
        } else if (operation == 3) {
#if defined(MIOS_TLS4_NATIVE_MBEDTLS36) && MIOS_TLS4_NATIVE_MBEDTLS36
            tls_rc = tls4_native_handshake_strict("example.com");
            if (tls_rc == 0) tls4_native_cleanup();
            term_print(tls_rc == 0 ? "TLS4.5: CERTIFICADO OK" :
                                  "TLS4.5: HANDSHAKE RECHAZADO");
#else
            serial_write("TLS4.3-4.5: MBED TLS NO ENLAZADO; NO ES HTTPS NATIVO\\n");
            term_print("TLS NATIVO: AUN NO INTEGRADO");
#endif
        }
    }
'''
s=rx.sub(lambda m:m.group(1)+code,s,count=1)
# Insertion is present exactly once, preserve original lab function.
if s.count('static int rtl8139_dma_arp_test(void)')!=1 or s.count('mios_tls4_stage.inc')!=1:
    fail('comprobacion final de simbolos no superada')
backup=kernel.with_name('kernel.c.pre-tls4-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
shutil.copy2(kernel,backup)
try:
    kernel.write_text(s,encoding='utf-8')
except Exception:
    shutil.copy2(backup,kernel)
    raise
print('MiOS TLS 4.1/4.2: instalado para pruebas (NO TLS nativo todavia)')
print('Respaldo:',backup)
print('Comandos: TLS CHECK, TLS TCP443, TLS NATIVE')
print('TLS 4.3-4.5: fuente estricta disponible, sin vincular Mbed TLS')
print('Proxima verificacion: make clean && make')
