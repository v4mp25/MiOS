#!/usr/bin/env python3
"""Host-side deterministic tests; does NOT touch MiOS project disk images."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='mios-dlstore-tests-') as tmp:
    tmp = Path(tmp)
    # The disk creator operates relative to cwd: isolated temporary directory.
    subprocess.run([sys.executable, str(ROOT / 'tools/crear_downloadstore.py')],
                   cwd=tmp, check=True, capture_output=True)
    disk = tmp / 'mios-downloadstore-10.img'
    exe = tmp / 'test_download_store'
    subprocess.run([
        'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'tests/mock_include'),
        '-I', str(ROOT / 'include'),
        str(ROOT / 'kernel/download_store.c'),
        str(ROOT / 'tests/test_download_store.c'),
        '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe), str(disk)], check=True)

    # Syntax + freestanding compilation tests. Real IRQ asm is not needed
    # for a syntax-only compile; production repo provides its own irq.h.
    for filename in ['drivers/ata_slave.c', 'kernel/download_store.c']:
        src = ROOT / filename
        subprocess.run([
            'gcc', '-m32', '-ffreestanding', '-fno-pie', '-Wall', '-Wextra',
            '-Werror', '-I', str(ROOT / 'tests/mock_include'),
            '-I', str(ROOT / 'include'), '-c', str(src),
            '-o', str(tmp / (src.stem + '.o')),
        ], check=True)
    # Freestanding compile check of the complete HTTP09 module with mock symbols.
    subprocess.run([
        'gcc','-m32','-ffreestanding','-fno-pie','-fno-stack-protector',
        '-Wall','-Wextra','-Werror','-Wno-unused-function',
        '-I',str(ROOT / 'include'),'-I',str(ROOT),
        '-c',str(ROOT / 'tests/test_http09_freestanding.c'),
        '-o',str(tmp / 'http09_stub.o'),
    ], check=True)
    # Native URL/HTTP parser tests with binary content.
    subprocess.run([
        'gcc','-O0','-Wall','-Wextra','-Werror','-Wno-unused-function',
        '-I',str(ROOT / 'include'),'-I',str(ROOT),
        str(ROOT / 'tests/test_http09_stub.c'),'-o',str(tmp / 'http09_test'),
    ], check=True)
    subprocess.run([str(tmp / 'http09_test')], check=True)
    subprocess.run([sys.executable, str(ROOT / 'tests/test_installer.py')],check=True)
print('COMPILACION FREESTANDING, PARSER BINARIO, INSTALADOR Y DISCO: OK')
