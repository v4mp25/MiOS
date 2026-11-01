
from pathlib import Path
import struct

IMAGE = Path("miosfs2-laboratorio.img")

SECTOR = 512
TOTAL_SECTORS = 32768

SUPER_LBA = 2048
BITMAP_LBA = 2049
NODES_LBA = 2050
DATA_LBA = 2058

MAGIC = b"MIOSFS02"


def fnv1a(data):
    h = 2166136261

    for byte in data:
        h ^= byte
        h = (h * 16777619) & 0xFFFFFFFF

    return h


# Formato: 8 bytes de firma + 14 enteros.
fields = [
    2,              # version
    SECTOR,         # sector_size
    TOTAL_SECTORS,  # total_sectors
    BITMAP_LBA,     # bitmap_lba
    1,              # bitmap_sectors
    NODES_LBA,      # nodes_lba
    8,              # nodes_sectors
    DATA_LBA,       # data_lba
    1024,           # data_sectors
    0,              # root_inode
    64,             # max_nodes
    0,              # flags
    0,              # checksum temporal
    0,              # reserved
]

superblock = bytearray(
    struct.pack("<8s14I", MAGIC, *fields)
)

# Checksum del superbloque de 64 bytes.
checksum = fnv1a(superblock)

# Campo checksum: bytes 56..59.
struct.pack_into("<I", superblock, 56, checksum)

# No sobrescribir discos existentes.
try:
    with IMAGE.open("xb") as disk:
        disk.truncate(TOTAL_SECTORS * SECTOR)

        disk.seek(SUPER_LBA * SECTOR)
        disk.write(superblock)

        disk.seek(BITMAP_LBA * SECTOR)
        disk.write(bytes(SECTOR))

except FileExistsError:
    raise SystemExit(
        "ERROR: el disco ya existe. "
        "No se sobrescribio."
    )

print("DISCO MIOSFS02 CREADO")
print("Imagen:", IMAGE)
print("Tamano:", TOTAL_SECTORS * SECTOR)
print("Superbloque LBA:", SUPER_LBA)
print("Bitmap LBA:", BITMAP_LBA)
print("Bloques de datos:", 1024)
print("Estado inicial: todos libres")
