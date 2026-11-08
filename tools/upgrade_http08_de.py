#!/usr/bin/env python3
"""Upgrade MiOS HTTP 0.8A-C to 0.8D/E + optional local HTTPS bridge.

Run from ~/MiOS: python3 tools/upgrade_http08_de.py
Requires HTTP 0.8A-C already installed. Never writes to disk images.
"""
from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

root = Path.cwd()
kernel = root / 'kernel/kernel.c'
old = root / 'kernel/net_public_08.inc'
new = root / 'kernel/net_public_08d.inc'

def die(message):
    raise SystemExit('HTTP08-DE ERROR: ' + message)

for item in (kernel, old, new):
    if not item.is_file():
        die('No existe: ' + str(item) + '. Ejecuta desde ~/MiOS.')

text = kernel.read_text(encoding='utf-8')
source = old.read_text(encoding='utf-8')
updated_inc = new.read_text(encoding='utf-8')
if 'h08_extract_body' in source or 'h08_set_url_from_gui' in text:
    die('Parece que ya instalaste HTTP 0.8D/E; no se duplicaran cambios.')

anchors = [
    'static volatile int mios_download_pending = 0;',
    'static int mios_public08_mode = 0;',
    'else if (term_equals(term_input, "DOWNLOAD PUBLIC"))',
    'static void term_execute(void)',
    '#include "net_public_08.inc"',
    'static int rtl8139_dma_arp_test(void)',
    'mios_http08_public_test()',
]
for a in anchors:
    if text.count(a) != 1:
        die(f'Ancla no unica o ausente (ocurrencias {text.count(a)}): {a}')
if 'static int h08_extract_body' not in updated_inc:
    die('El modulo nuevo no contiene el receptor HTTP 0.8D')

work = text
work = work.replace('static volatile int mios_download_pending = 0;',
'''/* HTTP08D/E: URL elegida desde el interprete grafico. */
static int h08_is_gui_url_command(const char *line);
static int h08_set_url_from_gui(const char *line);
static const char *h08_get_filename(void);
static int h08_using_bridge(void);
static volatile int mios_download_pending = 0;''',1)

case = '''else if (h08_is_gui_url_command(term_input)) {
    if (mios_download_already_run) {
        term_print("DOWNLOAD: SOLO UNO POR ARRANQUE");
    } else if (mios_download_pending) {
        term_print("DOWNLOAD: YA ESTA PENDIENTE");
    } else if (h08_set_url_from_gui(term_input) != 0) {
        term_print("DOWNLOAD: URL/NOMBRE INVALIDO");
    } else {
        mios_public08_mode = 1;
        mios_download_pending = 1;
        term_print("HTTP08: SOLICITUD REGISTRADA");
        if (h08_using_bridge())
            term_print("HTTPS: TLS SE HACE EN WSL");
    }
}
'''
work = work.replace('else if (term_equals(term_input, "DOWNLOAD PUBLIC"))',
                    case + 'else if (term_equals(term_input, "DOWNLOAD PUBLIC"))',1)

# Improve the already-implemented GUI success message, but do not touch
# the old local-download success branch.
pattern = re.compile(
    r'term_print\s*\(\s*"HTTP PUBLICO: RESPUESTA RECIBIDA"\s*\)\s*;\s*'
    r'term_print\s*\(\s*"VER DETALLES EN COM1"\s*\)\s*;'
)
msg_new = ('''term_print("HTTP08: ARCHIVO BINFS OK");
                    term_print(h08_get_filename());
                    if (h08_using_bridge())
                        term_print("HTTPS VIA WSL (NO NATIVO)");''')
work, hits = pattern.subn(msg_new, work)
if hits != 1:
    die('No encontre un unico bloque de mensajes publicos. No se modifico nada.')

help_old = 'term_print("DOWNLOAD PUBLIC");'
if help_old in work:
    work = work.replace(help_old,
        help_old + '\n        term_print("DOWNLOAD HTTP HOST/RUTA");'
                   '\n        term_print("DOWNLOAD HTTPS HOST/RUTA");',1)

if work == text:
    die('No hubo cambios en kernel.c')

stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup_kernel = kernel.with_name('kernel.c.pre-http08de-' + stamp)
backup_inc = old.with_name('net_public_08.inc.pre-http08de-' + stamp)
if backup_kernel.exists() or backup_inc.exists():
    die('Respaldo con ese nombre ya existe')
shutil.copy2(kernel, backup_kernel)
shutil.copy2(old, backup_inc)
try:
    old.write_text(updated_inc, encoding='utf-8')
    kernel.write_text(work, encoding='utf-8')
except Exception:
    shutil.copy2(backup_kernel, kernel)
    shutil.copy2(backup_inc, old)
    raise
print('HTTP08D/E: MODULO ACTUALIZADO')
print('Kernel backup:', backup_kernel)
print('Module backup:', backup_inc)
print('BINFS HTTP publico habilitado, URL variable habilitada')
print('HTTPS: solo mediante puente WSL opcional, no TLS nativo')
print('Ahora compila: make clean && make')
