#!/usr/bin/env python3
"""Instalar MiOS HTTP 0.8A-0.8C sin borrar el laboratorio HTTP 0.7.

Se ejecuta desde la raiz del repositorio MiOS con:
    python3 tools/instalar_http08.py

No utiliza Internet ni escribe en discos .img.
"""
from pathlib import Path
import re
import shutil
import sys
from datetime import datetime

ROOT = Path.cwd()
KERNEL = ROOT / 'kernel' / 'kernel.c'
INC = ROOT / 'kernel' / 'net_public_08.inc'


def fail(message):
    print('HTTP08 INSTALL ERROR:', message)
    sys.exit(1)

if not KERNEL.is_file():
    fail('ejecuta desde ~/MiOS; falta kernel/kernel.c')
if not INC.is_file():
    fail('falta kernel/net_public_08.inc, copia ese archivo primero')

source = KERNEL.read_text(encoding='utf-8')
if 'mios_http08_public_test' in source or 'net_public_08.inc' in source:
    fail('la integracion HTTP08 parece estar instalada; no repetimos cambios')

# Require known-safe anchors from the user's known functional 0.7C version.
required = [
    'static volatile int mios_download_pending = 0;',
    'static int rtl8139_dma_arp_test(void)',
    'static void term_execute(void)',
    'static int mios_download_run_lab(void)',
    'else if (term_equals(term_input, "DOWNLOAD"))',
    'int download_result',
    'DOWNLOAD: 3072 BYTES OK',
    'BINFS: http3072.bin OK',
]
for anchor in required:
    if anchor not in source:
        fail(f'no encuentro ancla: {anchor!r}; no modifique nada')

work = source

# Forward declaration for term_execute and a mode latch consumed by GUI event loop.
anchor = 'static volatile int mios_download_pending = 0;'
if work.count(anchor) != 1:
    fail('la variable mios_download_pending aparece varias veces')
work = work.replace(anchor,
    '/* HTTP 0.8A-C: primera prueba HTTP publica desde el escritorio. */\n'
    'static int mios_http08_public_test(void);\n'
    'static int mios_public08_mode = 0;\n'
    + anchor, 1)

# Add command in graphical term_execute(), immediately before the existing lab case.
anchor = 'else if (term_equals(term_input, "DOWNLOAD"))'
if work.count(anchor) != 1:
    fail('el caso DOWNLOAD grafico no es unico')
public_cmd = '''else if (term_equals(term_input, "DOWNLOAD PUBLIC")) {
    if (mios_download_already_run) {
        term_print("PUBLIC: YA SE EJECUTO DOWNLOAD");
    } else if (mios_download_pending) {
        term_print("PUBLIC: SOLICITUD PENDIENTE");
    } else {
        mios_public08_mode = 1;
        mios_download_pending = 1;
        term_print("PUBLIC: SOLICITUD REGISTRADA");
    }
}
'''
work = work.replace(anchor, public_cmd + anchor, 1)

# The engine is added before the lab function, so it sees RTL I/O helpers,
# RX buffer, IP packet builders, and serial diagnostics.
anchor = 'static int rtl8139_dma_arp_test(void)'
if work.count(anchor) != 1:
    fail('la definicion rtl8139_dma_arp_test no es unica')
work = work.replace(anchor,
    '/* HTTP 0.8A-0.8C -- modulo aislado, sin alterar TCP 0.7. */\n'
    '#include "net_public_08.inc"\n\n' + anchor, 1)

# Make the existing GUI dispatcher choose public or lab, preserving one request/boot.
matcher = re.compile(r'\bint\s+download_result\s*=\s*mios_download_run_lab\s*\(\s*\)\s*;')
work, hits = matcher.subn(
    'int download_result = mios_public08_mode\n'
    '    ? mios_http08_public_test()\n'
    '    : mios_download_run_lab();', work)
if hits != 1:
    fail('no encontre una unica llamada GUI int download_result = mios_download_run_lab()')

# Do not lie to the user by claiming a public HTTP response was stored in BINFS.
matcher = re.compile(
    r'term_print\s*\(\s*"DOWNLOAD: 3072 BYTES OK"\s*\)\s*;\s*'
    r'term_print\s*\(\s*"BINFS: http3072\.bin OK"\s*\)\s*;')
replacement = '''if (mios_public08_mode) {
                    term_print("HTTP PUBLICO: RESPUESTA RECIBIDA");
                    term_print("VER DETALLES EN COM1");
                } else {
                    term_print("DOWNLOAD: 3072 BYTES OK");
                    term_print("BINFS: http3072.bin OK");
                }'''
work, hits = matcher.subn(replacement, work)
if hits != 1:
    fail('no pude adaptar mensajes de exito GUI; no modifique nada')

# Show the new command in terminal help if an identifiable HELP string exists.
for help_text in (
    'term_print("LS CAT DOWNLOAD");',
    'term_print("HELP CLEAR ECHO MEM VER FILES");',
):
    if help_text in work:
        work = work.replace(help_text,
            help_text + '\n        term_print("DOWNLOAD PUBLIC");', 1)
        break

# Use a distinct backup filename on every attempt; never clobber.
stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
backup = KERNEL.with_name('kernel.c.pre-http08-' + stamp)
if backup.exists():
    fail('ya existe el respaldo ' + str(backup))
shutil.copy2(KERNEL, backup)
try:
    KERNEL.write_text(work, encoding='utf-8')
except Exception:
    shutil.copy2(backup, KERNEL)
    raise

print('HTTP08: parche aplicado correctamente')
print('Respaldo:', backup)
print('Fuente:', INC)
print('Nuevo comando en Terminal grafica: DOWNLOAD PUBLIC')
print('El comando DOWNLOAD anterior se conserva.')
print('Ahora: make clean && make')
