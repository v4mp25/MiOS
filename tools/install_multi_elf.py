
from pathlib import Path
import struct

DISK = Path("mios-disk-multi-test.img")

FILES = [
    ("hola.elf", Path("user/hola_c.elf")),
    ("prueba.elf", Path("user/prueba_write.elf")),
    ("calculo.elf", Path("user/calculo.elf")),
]

CATALOG_LBA = 512
DATA_LBA = 513

SECTOR_SIZE = 512
SECTORS_PER_FILE = 16
MAX_FILE_SIZE = SECTOR_SIZE * SECTORS_PER_FILE

ENTRY_START = 16
ENTRY_SIZE = 64

MAGIC = b"MIOSBIN1"


def checksum(data):
    value = 2166136261

    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF

    return value


if not DISK.is_file():
    raise SystemExit("ERROR: no existe disco de prueba")

catalog = bytearray(SECTOR_SIZE)
catalog[0:8] = MAGIC

struct.pack_into("<I", catalog, 8, len(FILES))

payloads = []

for index, (name, path) in enumerate(FILES):
    if not path.is_file():
        raise SystemExit(f"ERROR: no existe {path}")

    data = path.read_bytes()
    filename = name.encode("ascii")

    if not data.startswith(b"\x7fELF"):
        raise SystemExit(f"ERROR: {path} no es ELF")

    if not 0 < len(data) <= MAX_FILE_SIZE:
        raise SystemExit(
            f"ERROR: tamano no admitido: {path}"
        )

    if len(filename) >= 32:
        raise SystemExit("ERROR: nombre demasiado largo")

    entry = ENTRY_START + index * ENTRY_SIZE

    if entry + ENTRY_SIZE > SECTOR_SIZE:
        raise SystemExit("ERROR: catalogo demasiado grande")

    lba = DATA_LBA + index * SECTORS_PER_FILE

    catalog[entry:entry + len(filename)] = filename

    struct.pack_into("<I", catalog, entry + 32, lba)
    struct.pack_into("<I", catalog, entry + 36, len(data))
    struct.pack_into(
        "<I", catalog, entry + 40, checksum(data)
    )

    payloads.append(
        data.ljust(MAX_FILE_SIZE, b"\x00")
    )

region_offset = CATALOG_LBA * SECTOR_SIZE

new_data = bytes(catalog) + b"".join(payloads)
region_size = len(new_data)

with DISK.open("r+b") as disk:
    disk.seek(0, 2)
    disk_size = disk.tell()

    if region_offset + region_size > disk_size:
        raise SystemExit("ERROR: region fuera del disco")

    disk.seek(region_offset)
    previous = disk.read(region_size)

    if len(previous) != region_size or any(previous):
        raise SystemExit(
            "ERROR: region no vacia o incompleta. "
            "No se modifico el disco."
        )

    disk.seek(region_offset)
    disk.write(new_data)
    disk.flush()

print("BINFS de tres archivos instalado correctamente")

for index, (name, path) in enumerate(FILES):
    print(
        name,
        "| bytes:", path.stat().st_size,
        "| LBA:", DATA_LBA + index * SECTORS_PER_FILE
    )

print("Disco:", DISK)
