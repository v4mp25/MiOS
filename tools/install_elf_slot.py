
from pathlib import Path
import struct

DISK = Path("mios-disk-elf-test.img")
ELF = Path("user/hola.elf")

SECTOR_SIZE = 512
HEADER_LBA = 256
DATA_LBA = 257
MAX_ELF_SIZE = 8192
DATA_SECTORS = MAX_ELF_SIZE // SECTOR_SIZE

MAGIC = b"MIOSELF1"


def fnv1a(data):
    value = 2166136261

    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF

    return value


if not DISK.is_file():
    raise SystemExit("ERROR: no existe el disco de prueba")

if not ELF.is_file():
    raise SystemExit("ERROR: no existe user/hola.elf")

data = ELF.read_bytes()

if not data.startswith(b"\x7fELF"):
    raise SystemExit("ERROR: archivo no es ELF")

if not 0 < len(data) <= MAX_ELF_SIZE:
    raise SystemExit("ERROR: ELF demasiado grande")

region_offset = HEADER_LBA * SECTOR_SIZE
region_size = (1 + DATA_SECTORS) * SECTOR_SIZE

with DISK.open("r+b") as disk:
    disk.seek(0, 2)
    disk_size = disk.tell()

    if region_offset + region_size > disk_size:
        raise SystemExit("ERROR: disco demasiado pequeno")

    disk.seek(region_offset)
    previous = disk.read(region_size)

    if any(previous):
        raise SystemExit(
            "ERROR: region no vacia. "
            "No se escribio ningun sector."
        )

    checksum = fnv1a(data)

    header = bytearray(SECTOR_SIZE)
    header[0:8] = MAGIC
    struct.pack_into("<I", header, 8, len(data))
    struct.pack_into("<I", header, 12, checksum)

    padded = data.ljust(MAX_ELF_SIZE, b"\x00")

    disk.seek(region_offset)
    disk.write(header)
    disk.write(padded)
    disk.flush()

print("ELF instalado correctamente")
print("Archivo:", ELF)
print("Tamano:", len(data), "bytes")
print("LBA cabecera:", HEADER_LBA)
print("LBA datos:", DATA_LBA)
print("Checksum:", hex(checksum))
print("Disco modificado:", DISK)
