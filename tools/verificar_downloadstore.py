#!/usr/bin/env python3
"""Offline read-only verification of MiOS DOWNLOADFS image and its entries."""
from pathlib import Path
import hashlib
import sys

DISK = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('mios-downloadstore-10.img')
SECTOR = 512
MAX_FILE_SIZE = 65536
ENTRY_COUNT = 12


def le32(b):
    return int.from_bytes(b, 'little')


def fnv(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def validated_catalog(cat):
    if len(cat) != 512 or cat[:8] != b'MIOSDL10':
        return False
    if (le32(cat[8:12]), le32(cat[12:16]), le32(cat[24:28]),
            le32(cat[28:32])) != (12, 65536, 1, 0):
        return False
    if le32(cat[16:20]) in (0, 0xFFFFFFFF):
        return False
    check = bytearray(cat)
    check[20:24] = b'\x00' * 4
    if fnv(check) != le32(cat[20:24]):
        return False
    for i in range(12):
        block = cat[32 + i * 40:32 + (i + 1) * 40]
        status = le32(block[32:36])
        if status not in (0, 1):
            return False
        if status == 0 and any(block):
            return False
        if status and not 1 <= le32(block[24:28]) <= 65536:
            return False
    return True


if not DISK.is_file():
    raise SystemExit(f'ERROR: no existe {DISK}')
with DISK.open('rb') as file:
    cats = [file.read(SECTOR), file.read(SECTOR)]
    valid = [validated_catalog(cat) for cat in cats]
    if not any(valid):
        raise SystemExit('ERROR: ambos catalogos DOWNLOADFS invalidos')
    if valid == [True, True]:
        # Choose generation with modulo difference (laboratory generation avoids wrap).
        cat = cats[1] if le32(cats[1][16:20]) > le32(cats[0][16:20]) else cats[0]
    else:
        cat = cats[0] if valid[0] else cats[1]
        print('ADVERTENCIA: solo una copia del catalogo es valida')
    print('DOWNLOADFS: catalogo valido, generacion', le32(cat[16:20]))
    count = 0
    for i in range(ENTRY_COUNT):
        entry = cat[32 + i * 40:32 + (i + 1) * 40]
        if le32(entry[32:36]) != 1:
            continue
        name = entry[:24].split(b'\x00', 1)[0].decode('ascii', 'strict')
        size = le32(entry[24:28])
        expected = le32(entry[28:32])
        file.seek((2 + i * 128) * SECTOR)
        body = file.read(size)
        if len(body) != size or fnv(body) != expected:
            raise SystemExit(f'ERROR: checksum/lectura invalido de {name}')
        print(f'{name}: {size} bytes, FNV32 OK, SHA256={hashlib.sha256(body).hexdigest()[:16]}...')
        if size == 16384 and body[:2] == b'\x00\xff':
            print('   Binario: comienzo 00 FF confirmado')
        count += 1
print(f'OK: {count} archivo(s), imagen leida sin modificaciones')
