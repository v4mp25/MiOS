#!/usr/bin/env python3
"""Create a small source-only ZIP for reviewing the current MiOS version.
Does not include .git, disk images, objects, private keys, certificates, or vendor TLS tree.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT=Path.cwd()
OUTPUT=ROOT.parent / 'MiOS-fase4-actual.zip'
SKIP_DIR={'build','__pycache__','.git','.venv','node_modules','dist','iso','.pytest_cache'}
SKIP_SUFFIX={'.img','.iso','.o','.a','.elf','.pcap','.qcow2','.raw','.bak','.zip','.pem','.key','.crt','.p12','.pfx','.log'}
SKIP_MARKERS=('.bak','-rescate','.pre-','.tmp','_backup')
INCLUDE_DIR=('kernel','include','drivers','net','fs','boot','tools','tests','lib','user')
FILES=('Makefile',)
count=0
if OUTPUT.exists():
    raise SystemExit(f'El archivo ya existe: {OUTPUT} (no sobrescribo)')
with ZipFile(OUTPUT,'w',ZIP_DEFLATED,compresslevel=8) as z:
    for name in FILES:
        p=ROOT/name
        if p.is_file():z.write(p,p.relative_to(ROOT));count+=1
    for folder in INCLUDE_DIR:
        p=ROOT/folder
        if not p.is_dir():continue
        for f in p.rglob('*'):
            if not f.is_file():continue
            parts=set(f.relative_to(ROOT).parts)
            if parts & SKIP_DIR or f.suffix.lower() in SKIP_SUFFIX:continue
            if any(x in f.name.lower() for x in SKIP_MARKERS):continue
            if f.stat().st_size>2_000_000:continue
            z.write(f,f.relative_to(ROOT));count+=1
    config=ROOT/'third_party/mbedtls/include/mbedtls/mios_config.h'
    if config.is_file():z.write(config,config.relative_to(ROOT));count+=1
print('PAQUETE FUENTES:',OUTPUT)
print('ARCHIVOS:',count)
print('REVISA el ZIP para descartar secretos antes de compartirlo.')
