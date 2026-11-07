
from pathlib import Path
import shutil
import os
import struct

SOURCE = Path("mios-http06-test.img")
DEST = Path("mios-http06-binfs-lab.img")

SECTOR = 512

PRIMARY_LBA = 512
MIRROR_LBA = 600

DATA_LBA = 513
SHADOW_LBA = 1024

SLOTS = 4
SECTORS_PER_FILE = 16

GEN_OFFSET = 480
HASH_OFFSET = 484
VERSION_OFFSET = 488

MAGIC = b"MIOSBIN1"

def fnv1a(data):
    value = 2166136261

    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF

    return value

def read_sector(f, lba):
    f.seek(lba * SECTOR)
    data = f.read(SECTOR)

    if len(data) != SECTOR:
        raise RuntimeError(
            f"Sector {lba} incompleto"
        )

    return data

if not SOURCE.is_file():
    raise SystemExit(
        "ERROR: disco origen no encontrado"
    )

if DEST.exists():
    raise SystemExit(
        "ERROR: el disco de laboratorio ya existe. "
        "No se sobrescribio."
    )

with SOURCE.open("rb") as disk:
    disk.seek(0, os.SEEK_END)
    disk_size = disk.tell()

    required_size = (
        SHADOW_LBA + SLOTS * SECTORS_PER_FILE
    ) * SECTOR

    if disk_size < required_size:
        raise SystemExit(
            "ERROR: disco demasiado pequeno"
        )

    # Los catalogos del disco origen
    # deben estar completamente vacios.
    for lba in (PRIMARY_LBA, MIRROR_LBA):
        block = read_sector(disk, lba)

        if any(block):
            raise SystemExit(
                f"ERROR: LBA {lba} contiene datos. "
                "No inicializar."
            )

    # Comprobar que las regiones de datos
    # previstas por BINFS no contengan
    # informacion previa.
    regions = (
        (DATA_LBA, SLOTS * SECTORS_PER_FILE),
        (SHADOW_LBA, SLOTS * SECTORS_PER_FILE),
    )

    for start, count in regions:
        for lba in range(start, start + count):
            if any(read_sector(disk, lba)):
                raise SystemExit(
                    f"ERROR: LBA {lba} no esta vacio. "
                    "No inicializar."
                )

# Crear catalogo BINFS v2:
# cuatro entradas vacias de 64 bytes.
catalog = bytearray(SECTOR)

catalog[:8] = MAGIC

struct.pack_into("<I", catalog, 8, SLOTS)
struct.pack_into("<I", catalog, GEN_OFFSET, 1)
struct.pack_into("<I", catalog, VERSION_OFFSET, 2)

struct.pack_into("<I", catalog, HASH_OFFSET, 0)

checksum = fnv1a(catalog)

struct.pack_into(
    "<I",
    catalog,
    HASH_OFFSET,
    checksum
)

# Crear una copia independiente.
shutil.copyfile(SOURCE, DEST)

try:
    with DEST.open("r+b") as disk:
        for lba in (PRIMARY_LBA, MIRROR_LBA):
            disk.seek(lba * SECTOR)
            disk.write(catalog)

        disk.flush()
        os.fsync(disk.fileno())

        for lba in (PRIMARY_LBA, MIRROR_LBA):
            actual = read_sector(disk, lba)

            if actual != bytes(catalog):
                raise RuntimeError(
                    "Error verificando catalogo"
                )

except Exception:
    DEST.unlink(missing_ok=True)
    raise

print("BINFS HTTP LAB: PREPARADO")
print("Archivo:", DEST)
print("Catalogos A/B: IDENTICOS")
print("Magic:", MAGIC.decode())
print("Slots:", SLOTS)
print("Generacion:", 1)
print("Version:", 2)
print("Checksum:", hex(checksum))
print("Disco original: SIN MODIFICAR")
