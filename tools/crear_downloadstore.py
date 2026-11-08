#!/usr/bin/env python3
"""Create a dedicated MiOS DOWNLOADFS volume for IDE SLAVE index=1.

Never formats or opens the user's primary mios-disk*.img; never overwrites
an existing path. Run ONLY with QEMU shut down.
"""
from pathlib import Path
import os
import struct

OUT = Path('mios-downloadstore-10.img')
SECTOR = 512
SLOTS = 12
SECTORS_PER_SLOT = 128
TOTAL_SECTORS = 2 + SLOTS * SECTORS_PER_SLOT
MAGIC = b'MIOSDL10'

if OUT.exists():
    raise SystemExit(f'ERROR: {OUT} ya existe, no lo sobrescribo')

catalog = bytearray(SECTOR)
catalog[:8] = MAGIC
struct.pack_into('<I', catalog, 8, SLOTS)
struct.pack_into('<I', catalog, 12, SECTORS_PER_SLOT * SECTOR)
struct.pack_into('<I', catalog, 16, 1)  # generation
struct.pack_into('<I', catalog, 20, 0)  # checksum excluded while hashing
struct.pack_into('<I', catalog, 24, 1)  # version

hash32 = 2166136261
for octet in catalog:
    hash32 = ((hash32 ^ octet) * 16777619) & 0xFFFFFFFF
struct.pack_into('<I', catalog, 20, hash32)

flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
fd = os.open(str(OUT), flags, 0o600)
try:
    os.ftruncate(fd, TOTAL_SECTORS * SECTOR)
    with os.fdopen(fd, 'r+b', closefd=False) as f:
        for lba in (0, 1):
            f.seek(lba * SECTOR)
            f.write(catalog)
        f.flush()
        os.fsync(f.fileno())
    with OUT.open('rb') as f:
        for lba in (0, 1):
            f.seek(lba * SECTOR)
            if f.read(SECTOR) != catalog:
                raise RuntimeError('Fallo de lectura del catalogo')
finally:
    os.close(fd)

print('DOWNLOADFS 1.0 CREADO (disco ATA esclavo)')
print('Archivo:', OUT)
print('Tamano:', TOTAL_SECTORS * SECTOR, 'bytes')
print('Archivos maximos:', SLOTS)
print('Maximo por archivo:', SECTORS_PER_SLOT * SECTOR, 'bytes')
print('Checksum catalogo:', hex(hash32))
print('Discos originales: SIN MODIFICAR')
