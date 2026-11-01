
from pathlib import Path
import struct
import shutil
import os

SOURCE = Path("mios-disk-antes-fallos.img")
TARGET = Path("mios-disk-catalogo-danado.img")

SECTOR_SIZE = 512
CATALOG_A = 512
CATALOG_B = 600

MAGIC = b"MIOSBIN1"
VERSION_OFFSET = 488
GENERATION_OFFSET = 480
CHECKSUM_OFFSET = 484

def checksum(data):
    value = 2166136261

    for i, byte in enumerate(data):
        if CHECKSUM_OFFSET <= i < CHECKSUM_OFFSET + 4:
            byte = 0

        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF

    return value

def read_sector(disk, lba):
    disk.seek(lba * SECTOR_SIZE)
    data = disk.read(SECTOR_SIZE)

    if len(data) != SECTOR_SIZE:
        raise RuntimeError("Sector incompleto")

    return data

def validate(data):
    if data[:8] != MAGIC:
        return False

    version = struct.unpack_from(
        "<I", data, VERSION_OFFSET
    )[0]

    generation = struct.unpack_from(
        "<I", data, GENERATION_OFFSET
    )[0]

    expected = struct.unpack_from(
        "<I", data, CHECKSUM_OFFSET
    )[0]

    slots = struct.unpack_from("<I", data, 8)[0]

    return (
        version == 2
        and generation > 0
        and 1 <= slots <= 4
        and checksum(data) == expected
    )

if not SOURCE.is_file():
    raise SystemExit("ERROR: no existe el disco origen")

if TARGET.exists():
    raise SystemExit(
        "ERROR: el disco de prueba ya existe. "
        "No se sobrescribio."
    )

with SOURCE.open("rb") as disk:
    a = read_sector(disk, CATALOG_A)
    b = read_sector(disk, CATALOG_B)

if not validate(a) or not validate(b):
    raise SystemExit(
        "ERROR: ambos catalogos deben estar "
        "validos antes de la prueba"
    )

gen_a = struct.unpack_from("<I", a, GENERATION_OFFSET)[0]
gen_b = struct.unpack_from("<I", b, GENERATION_OFFSET)[0]

if gen_a == gen_b:
    if a != b:
        raise SystemExit(
            "ERROR: generaciones iguales "
            "con catalogos diferentes"
        )
    active_lba = CATALOG_A
else:
    active_lba = CATALOG_A if gen_a > gen_b else CATALOG_B

# Crear una copia descartable.
shutil.copyfile(SOURCE, TARGET)

with TARGET.open("r+b") as disk:
    offset = (
        active_lba * SECTOR_SIZE + CHECKSUM_OFFSET
    )

    disk.seek(offset)
    previous = disk.read(1)

    if len(previous) != 1:
        raise RuntimeError("Lectura incompleta")

    disk.seek(offset)

    # Alterar un byte del checksum.
    disk.write(bytes([previous[0] ^ 0x01]))

    disk.flush()
    os.fsync(disk.fileno())

    damaged = read_sector(disk, active_lba)

    if validate(damaged):
        raise RuntimeError(
            "No se logro invalidar el catalogo"
        )

print("SIMULACION PREPARADA")
print("Generacion A:", gen_a)
print("Generacion B:", gen_b)
print("Catalogo danado LBA:", active_lba)
print("Se modifico solo un byte del checksum")
print("Disco de prueba:", TARGET)
print("Disco original conservado:", SOURCE)
