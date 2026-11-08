#!/usr/bin/env python3
"""Check TLS C-compat i386 object and optionally compile Mbed TLS AES/SHA256.
Requires GCC/binutils. No changes to kernel or disk images.
"""
from __future__ import annotations
import argparse
import subprocess
from pathlib import Path

BASE_FLAGS = [
    "-m32", "-std=c11", "-ffreestanding", "-fno-builtin", "-fno-pie",
    "-fno-pic", "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
    "-U_FORTIFY_SOURCE", "-O2", "-Wall", "-Wextra"
]
BANNED = ("__printf_chk", "__memcpy_chk", "mbedtls_aesni_", "mbedtls_padlock_")


def run(args, cwd=None):
    print("$", " ".join(map(str, args)), flush=True)
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--only-libc", action="store_true", help="do not require Mbed TLS source")
    args = p.parse_args()
    project = Path.cwd()
    out = project / "build/mbedtls-mios"
    out.mkdir(parents=True, exist_ok=True)
    libc = out / "mios_tls_libc.o"
    src = project / "lib/mios_tls_libc.c"
    hdr = project / "include/mios_tls_libc.h"
    if not src.is_file() or not hdr.is_file():
        raise SystemExit("FALTAN lib/mios_tls_libc.c o include/mios_tls_libc.h; extrae el ZIP en MiOS")
    jobs = [(src, libc, ["-Iinclude"])]
    if not args.only_libc:
        tls = project / "third_party/mbedtls"
        cfg = tls / "include/mbedtls/mios_config.h"
        if not cfg.is_file():
            raise SystemExit("FALTA mios_config.h; ejecuta preparar_config_tls.py --apply")
        flags = ["-Ithird_party/mbedtls/include", "-Ithird_party/mbedtls/library",
                 '-DMBEDTLS_CONFIG_FILE="mbedtls/mios_config.h"']
        jobs.extend([(tls/"library/aes.c", out/"aes.o", flags),
                     (tls/"library/sha256.c", out/"sha256.o", flags)])
    bad = 0
    for path, dest, extra in jobs:
        cmd = ["gcc", *BASE_FLAGS, *extra, "-c", str(path), "-o", str(dest)]
        res = run(cmd)
        if res.returncode != 0:
            print(res.stderr[-3500:])
            raise SystemExit(f"ERROR COMPILANDO: {path}")
        kind = run(["file", str(dest)])
        print(kind.stdout.strip())
        if "ELF 32-bit" not in kind.stdout:
            raise SystemExit(f"ERROR ARQUITECTURA: {dest}")
        symbols = run(["nm", "-u", str(dest)])
        refs = symbols.stdout.strip()
        print(f"SIMBOLOS INDEFINIDOS {dest.name}:\n{refs or '(ninguno)'}")
        bad_syms = [x for x in BANNED if x in refs]
        if bad_syms:
            print("DEPENDENCIAS NO DESEADAS:", bad_syms)
            bad += len(bad_syms)
        if dest == libc and refs:
            raise SystemExit("ERROR: la compatibilidad freestanding no debe tener dependencias")
    if bad:
        raise SystemExit("ERROR: modifica configuracion antes de continuar; NO enlazar kernel")
    print("AUDITORIA TLS 4.3: OK (solo objetos individuales; NO handshake ni enlace)")
    print("SIGUIENTE: revisar dependencias de TODA la biblioteca y compatibilidad kernel")

if __name__ == "__main__":
    main()
