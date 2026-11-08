#!/usr/bin/env python3
"""MiOS TLS 4.3C: read-only audit or safe preparation.
Never enables native TLS or rewrites kernel sources.
Uses official local Mbed TLS sources supplied by owner.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import shutil
import re
from datetime import datetime

ROOT = Path(__file__).resolve().parent.parent
CORE = ('kernel/kernel.c', 'Makefile', 'kernel/net_public_09.inc', 'kernel/mios_tls4_mbedtls36.inc')
CONFIG = ROOT / 'third_party/mbedtls/include/mbedtls/mios_config.h'
LIBDIR = ROOT / 'build/mbedtls-i386/library'
REPORT = ROOT / 'build/tls43/reporte.txt'
NECESSARY = ('libmbedcrypto.a', 'libmbedx509.a', 'libmbedtls.a')


def runner(args, *, cwd=ROOT, log=None):
    print('$', ' '.join(map(str,args)), flush=True)
    p = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log is not None:
        log.append('$ ' + ' '.join(map(str,args)) + '\n' + p.stdout)
    if p.stdout:
        print(p.stdout[-7000:], end='\n' if not p.stdout.endswith('\n') else '', flush=True)
    return p.returncode, p.stdout


def get_symbols(archives):
    defined = set()
    used = set()
    for archive in archives:
        status, output = runner(['nm','-g','-P',str(archive)])
        if status != 0:
            raise RuntimeError('nm no pudo inspeccionar '+str(archive))
        for raw in output.splitlines():
            bits = raw.split()
            if len(bits) < 2 or bits[0].endswith(':'):
                continue
            sym, typ = bits[:2]
            if typ == 'U':
                used.add(sym)
            elif typ in {'T','D','B','R','C','G','S','W','V','t','d','b','r'}:
                defined.add(sym)
    return defined, used


def analyze(report):
    report.append('MiOS TLS 4.3C — auditoria de integracion (NO HTTPS operativo)')
    report.append('Proyecto: '+str(ROOT))
    report.append('')
    for path in CORE:
        ok = (ROOT / path).is_file()
        report.append(('OK ' if ok else 'FALTA ') + path)
    if not CONFIG.is_file():
        report.append('FALTA mios_config.h')
    else:
        s = CONFIG.read_text(errors='replace')
        macros = ('MBEDTLS_SSL_CLI_C','MBEDTLS_X509_CRT_PARSE_C','MBEDTLS_CTR_DRBG_C',
                  'MBEDTLS_ENTROPY_C','MBEDTLS_PLATFORM_MEMORY','MBEDTLS_PLATFORM_TIME_ALT',
                  'MBEDTLS_PLATFORM_GMTIME_R_ALT','MBEDTLS_SSL_KEEP_PEER_CERTIFICATE')
        for name in macros:
            enabled = bool(re.search(r'^\s*#define\s+'+name+r'\b',s,re.M))
            report.append(('OK ' if enabled else 'REVISAR ') + name)
    actual_src = ROOT / 'third_party/mbedtls/library/ssl_tls.c'
    if not actual_src.exists():
        report.append('FALTAN FUENTES: third_party/mbedtls/library/ssl_tls.c — no estan en el ZIP')
    else:
        report.append('OK fuentes Mbed TLS locales')
    roots = ROOT / 'kernel/mios_tls4_roots.inc'
    report.append(('EXISTE ' if roots.exists() else 'FALTA ') + 'kernel/mios_tls4_roots.inc (CA PEM revisado)')
    kernel = (ROOT/'kernel/kernel.c').read_text(errors='replace') if (ROOT/'kernel/kernel.c').exists() else ''
    report.append('TLS nativo en GUI: '+('todavia informa no integrado' if 'TLS NATIVO: NO INTEGRADO' in kernel else 'revisar implementacion'))
    for lib in NECESSARY:
        p = LIBDIR/lib
        report.append(('OK ' if p.exists() else 'FALTA ')+str(p.relative_to(ROOT)))
    libs = [LIBDIR/x for x in NECESSARY]
    if all(x.exists() for x in libs):
        for p in libs:
            code, out = runner(['file',str(p)])
            if code: report.append('ERROR file '+str(p))
            code, out = runner(['ar','t',str(p)])
            if code or not out.strip(): report.append('ERROR archivo vacio '+str(p))
        defined, used = get_symbols(libs)
        external = sorted(used - defined)
        report.extend(['','Simbolos externos a los tres archivos Mbed TLS: '+str(len(external))])
        report.extend('  U '+x for x in external)
        suspicious = [x for x in external if x.startswith('__') or x in ('fopen','fread','fwrite','pthread_create','socket','connect')]
        report.extend(['','SIMBOLOS DE RIESGO/ENTORNO ANFITRION: '+(', '.join(suspicious) if suspicious else '(ninguno detectado)')])
        report.append('Nota: nm de archivos .a no demuestra enlace final, puede incluir objetos no usados.')
    else:
        report.append('No hay librerias i386 completas todavia. Use --build en su Ubuntu.')
    report.extend(['','BLOQUEOS PARA TLS NATIVO: enlazado kernel, transporte TCP fiable,',
                   'reloj UTC confiable, entropia evaluada, CA valida, validacion de cadena/hostname.',
                   'NO activar MIOS_TLS4_NATIVE_MBEDTLS36=1.'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build',action='store_true',help='Compilar Mbed TLS i386 desde FUENTES LOCALES (sin alterar kernel).')
    args = ap.parse_args()
    if not (ROOT/'kernel/kernel.c').is_file():
        raise SystemExit('Ejecutar desde el arbol MiOS: tools/tls43_integrar_preparacion.py')
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    log = []
    if args.build:
        if not (ROOT/'third_party/mbedtls/CMakeLists.txt').exists():
            print('ERROR: faltan fuentes Mbed TLS (CMakeLists.txt). No se descargan automaticamente.')
            return 2
        script = ROOT/'tools/compilar_mbedtls_i386_cmake.sh'
        if not script.exists():
            print('ERROR: falta tools/compilar_mbedtls_i386_cmake.sh')
            return 2
        status, _ = runner(['bash',str(script)],log=log)
        if status:
            REPORT.write_text('COMPILACION TLS i386 FALLO.\n'+'\n'.join(log))
            print('Error de compilacion. Log:',REPORT)
            return status
    report = []
    analyze(report)
    REPORT.write_text('\n'.join(report)+'\n')
    print('\n'.join(report)[-12000:])
    print('REPORTE:',REPORT)
    return 0

if __name__ == '__main__':
    sys.exit(main())
