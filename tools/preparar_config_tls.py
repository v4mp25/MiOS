#!/usr/bin/env python3
"""Prepare Mbed TLS config for a MiOS freestanding build.
Never edits the vendor default config; backs up the existing MiOS config.
Run in the MiOS repo root. This does NOT activate native TLS.
"""
from __future__ import annotations
import argparse
import re
import shutil
from pathlib import Path

DISABLE = (
    "MBEDTLS_AESNI_C", "MBEDTLS_PADLOCK_C", "MBEDTLS_SELF_TEST",
    "MBEDTLS_NET_C", "MBEDTLS_TIMING_C", "MBEDTLS_FS_IO",
    "MBEDTLS_ENTROPY_PLATFORM",
)
REQUIRED = (
    "MBEDTLS_PLATFORM_C", "MBEDTLS_PLATFORM_MEMORY",
    "MBEDTLS_SSL_CLI_C", "MBEDTLS_SSL_TLS_C",
    "MBEDTLS_X509_CRT_PARSE_C", "MBEDTLS_CTR_DRBG_C",
    "MBEDTLS_ENTROPY_C", "MBEDTLS_HAVE_TIME", "MBEDTLS_HAVE_TIME_DATE",
    "MBEDTLS_PLATFORM_TIME_ALT", "MBEDTLS_PLATFORM_GMTIME_R_ALT",
)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--apply", action="store_true", help="apply after preview")
    a = p.parse_args()
    root = Path.cwd()
    vendor = root / "third_party/mbedtls/include/mbedtls/mbedtls_config.h"
    target = root / "third_party/mbedtls/include/mbedtls/mios_config.h"
    if not vendor.is_file():
        raise SystemExit("FALTA Mbed TLS en third_party/mbedtls: no se modifica nada")
    original = target.read_text() if target.is_file() else vendor.read_text()
    updated = original
    changed = []
    for name in DISABLE:
        pattern = re.compile(r"(?m)^(\s*)#\s*define\s+(" + re.escape(name) + r")(\b[^\n]*)$")
        updated, n = pattern.subn(r"\1//#define \2\3", updated)
        if n:
            changed.append(name)
    missing = [m for m in REQUIRED if not re.search(r"(?m)^\s*#\s*define\s+" + re.escape(m) + r"\b", updated)]
    print("CONFIG: original:", target if target.exists() else vendor)
    print("DESACTIVAR:", ", ".join(changed) if changed else "ninguno (ya estaban desactivados)")
    print("REQUISITOS NO ACTIVOS:", ", ".join(missing) if missing else "ninguno")
    if not a.apply:
        print("MODO LECTURA: para aplicar usa --apply")
        return
    if missing:
        raise SystemExit("ERROR: habilita primero los requisitos que faltan; no se escribio el archivo")
    if target.is_file() and updated != original:
        backup = target.with_suffix(".h.pre_tls43")
        if not backup.exists():
            shutil.copy2(target, backup)
            print("RESPALDO:", backup)
    if updated != original or not target.exists():
        target.write_text(updated)
        print("CONFIG ACTUALIZADA:", target)
    else:
        print("CONFIG SIN CAMBIOS")
    print("TLS NATIVO: NO ACTIVADO; falta compilar y enlazar la biblioteca completa")

if __name__ == "__main__":
    main()
