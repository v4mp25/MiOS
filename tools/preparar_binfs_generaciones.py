
from pathlib import Path
import struct
import shutil
import os

DISK = Path("mios-disk-generation-test.img")
TEMP = Path("mios-disk-generation-test.tmp")

SECTOR = 512
PRIMARY_LBA = 512
MIRROR_LBA = 600

GEN_OFFSET = 480
HASH_OFFSET = 484
VERSION_OFFSET = 488

MAGIC = b"MIOSBIN1"
VERSION = 2


def fnv1a(data):
    value = 2166136261

    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF

    return value


def read_sector(disk, lba):
    disk.seek(lba * SECTOR)
    data = disk.read(SECTOR)

    if len(data) != SECTOR:
        raise RuntimeError("Sector incompleto")

    return data


if not DISK.is_file():
    raise SystemExit("ERROR: disco no encontrado")

if TEMP.exists():
    raise SystemExit(
        "ERROR: archivo temporal existente. "
        "Revisarlo antes de continuar."
    )

with DISK.open("rb") as disk:
    primary = read_sector(disk, PRIMARY_LBA)
    mirror = read_sector(disk, MIRROR_LBA)

if primary != mirror:
    raise SystemExit(
        "ERROR: catalogos diferentes. "
        "No se modifico el disco."
    )

if primary[:8] != MAGIC:
    raise SystemExit("ERROR: firma BINFS incorrecta")

slots = struct.unpack_from("<I", primary, 8)[0]

if not 1 <= slots <= 4:
    raise SystemExit("ERROR: cantidad de slots invalida")

# El formato anterior no utiliza estos bytes.
if any(primary[GEN_OFFSET:VERSION_OFFSET + 4]):
    raise SystemExit(
        "ERROR: campos reservados ocupados. "
        "No se modifico el disco."
    )

catalog = bytearray(primary)

# Primera generacion de ambos catalogos.
struct.pack_into("<I", catalog, GEN_OFFSET, 1)

# Identificador del nuevo formato.
struct.pack_into("<I", catalog, VERSION_OFFSET, VERSION)

# Calcular checksum con su propio campo a cero.
struct.pack_into("<I", catalog, HASH_OFFSET, 0)
checksum = fnv1a(catalog)

struct.pack_into("<I", catalog, HASH_OFFSET, checksum)

try:
    # Modificar una copia temporal, no el archivo origen.
    shutil.copyfile(DISK, TEMP)

    with TEMP.open("r+b") as disk:
        for lba in (PRIMARY_LBA, MIRROR_LBA):
            disk.seek(lba * SECTOR)
            disk.write(catalog)

        disk.flush()
        os.fsync(disk.fileno())

        for lba in (PRIMARY_LBA, MIRROR_LBA):
            if read_sector(disk, lba) != bytes(catalog):
                raise RuntimeError(
                    "Fallo verificando el catalogo"
                )

    # Publicar la imagen modificada.
    os.replace(TEMP, DISK)

except Exception:
    print("ERROR: no se pudo preparar el formato")
    print("El temporal puede requerir revision.")
    raise

print("BINFS GENERACION 1 PREPARADA")
print("Formato:", VERSION)
print("Generacion:", 1)
print("Checksum:", hex(checksum))
print("Catalogo A: LBA", PRIMARY_LBA)
print("Catalogo B: LBA", MIRROR_LBA)
print("Ambos catalogos identicos: OK")
