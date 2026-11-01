
from pathlib import Path
import struct

DISK = Path("mios-disk-c-test.img")
ELF = Path("user/hola_c.elf")

CATALOG_LBA = 512
DATA_LBA = 513
SECTOR_SIZE = 512
MAX_SIZE = 8192
MAGIC = b"MIOSBIN1"

def checksum(data):
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value

if not DISK.exists() or not ELF.exists():
    raise SystemExit("ERROR: falta el disco o el ELF")

data = ELF.read_bytes()
name = b"hola.elf"

if not data.startswith(b"\x7fELF"):
    raise SystemExit("ERROR: archivo ELF invalido")

if len(data) == 0 or len(data) > MAX_SIZE:
    raise SystemExit("ERROR: tamano no admitido")

region_offset = CATALOG_LBA * SECTOR_SIZE
region_size = SECTOR_SIZE + MAX_SIZE

with DISK.open("r+b") as disk:
    disk.seek(0, 2)
    disk_size = disk.tell()

    if region_offset + region_size > disk_size:
        raise SystemExit("ERROR: region fuera del disco")

    disk.seek(region_offset)
    previous = disk.read(region_size)

    if len(previous) != region_size or any(previous):
        raise SystemExit(
            "ERROR: region no vacia o incompleta; "
            "no se escribio nada"
        )

    catalog = bytearray(SECTOR_SIZE)

    # Cabecera
    catalog[0:8] = MAGIC

    # Numero de archivos: 1
    struct.pack_into("<I", catalog, 8, 1)

    # Primera entrada: comienza en byte 16
    catalog[16:16 + len(name)] = name

    # Campos de la entrada
    struct.pack_into("<I", catalog, 48, DATA_LBA)
    struct.pack_into("<I", catalog, 52, len(data))
    struct.pack_into("<I", catalog, 56, checksum(data))

    padded = data.ljust(MAX_SIZE, b"\x00")

    # Preparar todo antes de escribir
    disk.seek(region_offset)
    disk.write(catalog)
    disk.write(padded)
    disk.flush()

print("Catalogo instalado correctamente")
print("Archivo:", name.decode())
print("Tamano:", len(data))
print("LBA catalogo:", CATALOG_LBA)
print("LBA datos:", DATA_LBA)
print("Checksum:", hex(checksum(data)))
