#!/usr/bin/env python3
"""Transactional local installer: MiOS DOWNLOAD 0.9 + DOWNLOADFS 1.0 + DOWNLOAD 1.0.

Run from ~/MiOS after unzipping the package. Does not run QEMU, access
network, touch any .img, or overwrite an existing backup.
"""
from pathlib import Path
from datetime import datetime
import re
import shutil
import sys

ROOT = Path.cwd()
KERNEL = ROOT / 'kernel/kernel.c'
MK = ROOT / 'Makefile'
REQUIRED = [
    KERNEL, MK,
    ROOT / 'kernel/net_public_08.inc',
    ROOT / 'kernel/net_public_09.inc',
    ROOT / 'kernel/download_store.c',
    ROOT / 'drivers/ata_slave.c',
    ROOT / 'include/download_store.h',
    ROOT / 'include/ata_slave.h',
]


def fail(message):
    raise SystemExit('DOWNLOAD10 INSTALL ERROR: ' + message)


for path in REQUIRED:
    if not path.is_file():
        fail('Falta ' + str(path) + ' (ejecuta desde ~/MiOS)')

kernel = KERNEL.read_text(encoding='utf-8')
makefile = MK.read_text(encoding='utf-8')
if 'net_public_09.inc' in kernel or 'download_store.o' in makefile:
    fail('DOWNLOAD10 parece ya instalado; no duplico los cambios')

anchors = (
    '#include "net_public_08.inc"',
    'static void term_execute(void)',
    'else if (h08_is_gui_url_command(term_input))',
    'else if (term_equals(term_input, "DOWNLOAD PUBLIC"))',
    'else if (term_equals(term_input, "DOWNLOAD"))',
    'static int rtl8139_dma_arp_test(void)',
    'mios_http08_public_test()',
)
for anchor in anchors:
    if kernel.count(anchor) != 1:
        fail('Ancla ausente/duplicada ' + repr(anchor))

# ---- Kernel: replace only .inc and adapt the graphical shell. ----
work = kernel.replace('#include "net_public_08.inc"',
                      '#include "net_public_09.inc"', 1)
# Make DOWNLOADFS prototypes visible to term_execute, defined before the .inc.
first_include = re.search(r'^#include [^\n]+\n', work, re.M)
if not first_include:
    fail('kernel.c no contiene ninguna directiva include')
work = work[:first_include.end()] + '#include "download_store.h"\n' + work[first_include.end():]

# Support multiple public/download-lab commands without changing the existing
# one-time restriction on the old local test DOWNLOAD.
old = '''if (mios_download_already_run) {
        term_print("DOWNLOAD: SOLO UNO POR ARRANQUE");
    } else if (mios_download_pending)'''
new = '''if (mios_download_pending)'''
if old not in work:
    fail('No localizo el bloqueo de una sola descarga en el comando URL')
work = work.replace(old, new, 1)

old = '''if (mios_download_already_run) {
        term_print("PUBLIC: YA SE EJECUTO DOWNLOAD");
    } else if (mios_download_pending)'''
if old not in work:
    fail('No localizo el bloqueo DOWNLOAD PUBLIC anterior')
work = work.replace(old, 'if (mios_download_pending)', 1)

# "DOWNLOAD PUBLIC" must reset to its original external test target even
# when another URL or a LAB request was previously used in this boot.
public_start = work.index('else if (term_equals(term_input, "DOWNLOAD PUBLIC"))')
public_end = work.index('else if (term_equals(term_input, "DOWNLOAD"))', public_start)
public_case = work[public_start:public_end]
if public_case.count('mios_public08_mode = 1;') != 1:
    fail('No encuentro el selector de destino DOWNLOAD PUBLIC')
public_case = public_case.replace('mios_public08_mode = 1;',
    'h08_set_url_from_gui("DOWNLOAD HTTP EXAMPLE.COM/");\n        mios_public08_mode = 1;', 1)
work = work[:public_start] + public_case + work[public_end:]

# Public requests may be repeated; the old local DOWNLOAD remains once per boot.
old = 'else if (mios_download_already_run) {\n\n            term_print('
if old not in work:
    # Accept indentation differences around the branch.
    rx = re.compile(r'else\s+if\s*\(mios_download_already_run\)\s*\{(?=\s*term_print\s*\()', re.M)
    work, hits = rx.subn('else if (mios_download_already_run && !mios_public08_mode) {', work)
    if hits != 1:
        fail(f'No localizo el bloqueo GUI ({hits})')
else:
    work = work.replace(old,
        'else if (mios_download_already_run && !mios_public08_mode) {\n\n            term_print(', 1)

rx = re.compile(r'\bmios_download_already_run\s*=\s*1\s*;(?=\s*term_print\s*\(\s*"DOWNLOAD: INICIANDO HTTP)', re.M)
work, hits = rx.subn('if (!mios_public08_mode) mios_download_already_run = 1;', work)
if hits != 1:
    fail(f'No localizo la marca de descarga unica en GUI ({hits})')

# For the old DOWNLOAD command only, reset previous public mode.
cmd_start = work.index('else if (term_equals(term_input, "DOWNLOAD"))')
brace = work.index('{', cmd_start)
level = 1
pos = brace + 1
while pos < len(work) and level:
    if work[pos] == '{': level += 1
    if work[pos] == '}': level -= 1
    pos += 1
if level != 0:
    fail('El bloque DOWNLOAD local no tiene las llaves completas')
cmd = work[cmd_start:pos]
if cmd.count('mios_download_pending = 1;') != 1:
    fail('No encuentro solicitud pendiente en DOWNLOAD local')
cmd = cmd.replace('mios_download_pending = 1;',
                  'mios_public08_mode = 0;\n        mios_download_pending = 1;', 1)
work = work[:cmd_start] + cmd + work[pos:]

new_case = '''else if (term_equals(term_input, "DOWNLOAD LS")) {
    int total = mios_dlstore_count();
    if (total < 0) {
        term_print("DLSTORE: DISCO EXTRA NO DISPONIBLE");
    } else {
        term_print_number("DLSTORE ARCHIVOS: ", (uint32_t)total);
        for (uint32_t i = 0U; i < MIOS_DLSTORE_MAX_FILES; i++) {
            MiosDlstoreEntry item;
            if (mios_dlstore_get_entry(i, &item) == 0)
                term_print(item.name);
        }
    }
}
'''
anchor = 'else if (h08_is_gui_url_command(term_input))'
work = work.replace(anchor, new_case + anchor, 1)

# GUI success displays DLSTORE instead of mistakenly claiming legacy BINFS.
work = work.replace('term_print("HTTP08: ARCHIVO BINFS OK");',
                    'term_print("DOWNLOADFS: ARCHIVO GUARDADO");', 1)
# Add help near the known public command.
help_anchor = 'term_print("DOWNLOAD HTTP HOST/RUTA");'
if help_anchor in work:
    work = work.replace(help_anchor,
        'term_print("DOWNLOAD LAB NOMBRE.BIN");\n        '
        'term_print("DOWNLOAD LS");\n        ' + help_anchor, 1)

# ---- Makefile: add both objects to dependencies, linker, and clean. ----
lines = makefile.splitlines(keepends=True)

match = re.findall(r'^kernel\.bin:.*$', makefile, re.M)
if len(match) != 1:
    fail('No encuentro una unica regla kernel.bin: en el Makefile')

m = makefile.replace(match[0], match[0] + ' ata_slave.o download_store.o', 1)
# Only modify the actual link recipe, not other $(LD) rules.
lines = m.splitlines(keepends=True)
header = next(i for i, line in enumerate(lines) if line.startswith('kernel.bin:'))
link_idx = None
for i in range(header + 1, min(header + 8, len(lines))):
    if lines[i].startswith('\t') and ('$(LD)' in lines[i] or 'ld ' in lines[i]):
        link_idx = i
        break
if link_idx is None:
    fail('No encuentro el comando de enlazado debajo de kernel.bin:')
lines[link_idx] = lines[link_idx].rstrip('\n') + ' ata_slave.o download_store.o\n'

clean_idx = next((i for i, line in enumerate(lines)
                  if line.startswith('\trm -f') and 'kernel.o' in line), None)
if clean_idx is None:
    fail('No encuentro limpieza de kernel.o')
lines[clean_idx] = lines[clean_idx].rstrip('\n') + ' ata_slave.o download_store.o\n'

# Add rules BEFORE the kernel.bin target.
header = next(i for i, line in enumerate(lines) if line.startswith('kernel.bin:'))
new_rules = '''ata_slave.o: drivers/ata_slave.c include/ata_slave.h
\t$(CC) $(CFLAGS) -c drivers/ata_slave.c -o ata_slave.o

download_store.o: kernel/download_store.c include/download_store.h include/ata_slave.h include/irq.h
\t$(CC) $(CFLAGS) -c kernel/download_store.c -o download_store.o

'''
lines.insert(header, new_rules)

# Recompile kernel.o after changing the embedded .inc or storage headers.
for i, line in enumerate(lines):
    if line.startswith('kernel.o:'):
        lines[i] = line.rstrip('\n') + ' kernel/net_public_09.inc include/download_store.h\n'
        break
else:
    fail('No encuentro regla kernel.o:')
new_mk = ''.join(lines)

# Pre-commit assertions: no old base .inc being actively included, no duplicated rules.
if work.count('#include "net_public_09.inc"') != 1 or \
   work.count('static int rtl8139_dma_arp_test(void)') != 1 or \
   new_mk.count('download_store.o:') != 1:
    fail('La integracion no paso las comprobaciones de seguridad')

stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup_kernel = KERNEL.with_name('kernel.c.pre-download10-' + stamp)
backup_makefile = MK.with_name('Makefile.pre-download10-' + stamp)
shutil.copy2(KERNEL, backup_kernel)
shutil.copy2(MK, backup_makefile)
try:
    KERNEL.write_text(work, encoding='utf-8')
    MK.write_text(new_mk, encoding='utf-8')
except Exception:
    shutil.copy2(backup_kernel, KERNEL)
    shutil.copy2(backup_makefile, MK)
    raise
print('DOWNLOAD 0.9, DOWNLOADFS 1.0 Y DOWNLOAD 1.0: INSTALADOS')
print('Kernel backup:', backup_kernel)
print('Makefile backup:', backup_makefile)
print('Comandos nuevos: DOWNLOAD LAB NOMBRE.BIN, DOWNLOAD LS')
print('Comando publico repetible: DOWNLOAD HTTP EXAMPLE.COM/ AS WEB1.HTML')
print('IMPORTANTE: necesita SEGUNDO DISCO IDE index=1, no modifica los existentes')
print('Compila con: make clean && make')
