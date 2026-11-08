#!/usr/bin/env python3
"""List unresolved external symbols in static Mbed TLS archives.
Can be used after an optional freestanding library build. No disk writes.
"""
from pathlib import Path
import subprocess
import sys

def sym(args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

if len(sys.argv) < 2:
    raise SystemExit("Uso: python3 tools/auditar_simbolos_tls.py build/mbedtls-i386/library")
base = Path(sys.argv[1])
libs = sorted(base.glob('libmbed*.a'))
if not libs:
    raise SystemExit(f"No se encontraron libmbed*.a en {base}")
# nm -g on archives prints U dependencies and external T/D/B symbols.
defined = set()
undefined = set()
for library in libs:
    r = sym(['nm', '-g', str(library)])
    if r.returncode:
        raise SystemExit(r.stderr)
    for line in r.stdout.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        # nm lines are either `U sym` or `addr T sym`.
        if len(fields) == 2 and fields[0] == 'U':
            undefined.add(fields[1])
        elif len(fields) >= 3 and fields[-2] not in ('U','u'):
            if fields[-2].isupper() and fields[-2] not in ('W', 'V'):
                defined.add(fields[-1])
external = sorted(undefined - defined)
print("BIBLIOTECAS:", ', '.join(p.name for p in libs))
print("SIMBOLOS EXTERNOS SIN DEFINICION EN ARCHIVOS Mbed TLS:",len(external))
for name in external:
    print("  U", name)
print("NOTA: faltan bibliotecas del kernel y del compilador; solo enlazado completo demuestra resolucion final.")
