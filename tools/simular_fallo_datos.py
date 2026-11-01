
from pathlib import Path
import struct
import shutil
import os

SOURCE = Path("mios-disk-fallo-ab-real.img")
TARGET = Path("mios-disk-data-failure-test.img")

SECTOR = 512
CATALOG_LBAS = (512, 600)

MAGIC = b"MIOSBIN1"
MAX_SLOTS = 4
ENTRY_START = 16
ENTRY_SIZE = 64
MAX_FILE_BYTES = 8192


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def fnv1a(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def catalog_hash(data):
    temp = bytearray(data)
    temp[484:488] = b"\x00" * 4
    return fnv1a(temp)


def read_catalog(disk, lba):
    disk.seek(lba * SECTOR)
    data = disk.read(SECTOR)

    if len(data) != SECTOR:
        raise ValueError("Lectura incompleta del catalogo")

    if data[:8] != MAGIC:
        raise ValueError("Firma de catalogo invalida")

    count = u32(data, 8)
    generation = u32(data, 480)
    version = u32(data, 488)

    if not (1 <= count <= MAX_SLOTS):
        raise ValueError("Cantidad de slots invalida")

    if version != 2 or generation == 0:
        raise ValueError("Version o generacion invalida")

    if catalog_hash(data) != u32(data, 484):
        raise ValueError("Checksum de catalogo invalido")

    entries = []

    for i in range(count):
        off = ENTRY_START + i * ENTRY_SIZE
        entry = data[off:off + ENTRY_SIZE]

        if entry[0] == 0:
            if any(entry):
                raise ValueError("Tombstone incompleto")
            continue

        if b"\x00" not in entry[:32]:
            raise ValueError("Nombre sin terminador")

        name = entry[:32].split(b"\x00", 1)[0]
        lba_file = u32(entry, 32)
        size = u32(entry, 36)
        expected_hash = u32(entry, 40)

        if not (0 < size <= MAX_FILE_BYTES):
            raise ValueError("Tamano de archivo invalido")

        region_a = 513 + i * 16
        region_b = 1024 + i * 16

        if lba_file not in (region_a, region_b):
            raise ValueError("LBA fuera de la region del slot")

        entries.append({
            "slot": i,
            "name": name,
            "lba": lba_file,
            "size": size,
            "hash": expected_hash,
        })

    return {
        "generation": generation,
        "entries": entries,
        "raw": data,
    }


def verify_files(disk, catalog):
    for entry in catalog["entries"]:
        disk.seek(entry["lba"] * SECTOR)
        data = disk.read(entry["size"])

        if len(data) != entry["size"]:
            return False

        if fnv1a(data) != entry["hash"]:
            return False

    return True


if not SOURCE.is_file():
    raise SystemExit("ERROR: falta el disco origen")

if TARGET.exists():
    raise SystemExit(
        "ERROR: disco destino ya existe; no sobrescribir"
    )

with SOURCE.open("rb") as disk:
    disk.seek(0, 2)
    disk_size = disk.tell()

    catalogs = [
        read_catalog(disk, lba)
        for lba in CATALOG_LBAS
    ]

    if not all(verify_files(disk, c) for c in catalogs):
        raise SystemExit(
            "ERROR: ambas generaciones deben tener "
            "archivos integros antes de esta prueba"
        )

    gen0 = catalogs[0]["generation"]
    gen1 = catalogs[1]["generation"]

    if gen0 == gen1:
        raise SystemExit(
            "ERROR: se necesitan dos generaciones "
            "distintas para esta simulacion"
        )

    newest = 0 if gen0 > gen1 else 1
    oldest = 1 - newest

    recent = catalogs[newest]
    previous = catalogs[oldest]

    # Proteger todos los sectores referenciados
    # por archivos de la version antigua.
    protected_sectors = set()

    for e in previous["entries"]:
        count = (e["size"] + SECTOR - 1) // SECTOR
        protected_sectors.update(
            range(e["lba"], e["lba"] + count)
        )

    candidate = None

    for e in recent["entries"]:
        # Elegir solamente un archivo cuya region
        # no intersecte los sectores protegidos.
        count = (e["size"] + SECTOR - 1) // SECTOR

        region = set(
            range(e["lba"], e["lba"] + count)
        )

        if region.isdisjoint(protected_sectors):
            candidate = e
            break

if candidate is None:
    raise SystemExit(
        "NO HAY REGION SEGURA PARA DANAR. "
        "El disco no fue modificado. "
        "Se necesita preparar otra pareja "
        "de generaciones independientes."
    )

shutil.copyfile(SOURCE, TARGET)

byte_offset = candidate["lba"] * SECTOR

with TARGET.open("r+b") as disk:
    disk.seek(byte_offset)
    original = disk.read(1)

    if len(original) != 1:
        raise SystemExit("ERROR: lectura de byte")

    disk.seek(byte_offset)
    disk.write(bytes([original[0] ^ 0x01]))

    disk.flush()
    os.fsync(disk.fileno())

    # La version antigua debe seguir integra.
    if not verify_files(disk, previous):
        raise RuntimeError(
            "ERROR: respaldo antiguo no integro"
        )

    # El archivo reciente debe fallar checksum.
    disk.seek(candidate["lba"] * SECTOR)
    damaged = disk.read(candidate["size"])

    if fnv1a(damaged) == candidate["hash"]:
        raise RuntimeError(
            "ERROR: no se detecto corrupcion"
        )

print("SIMULACION DE DATOS PREPARADA")
print("Generacion antigua:", previous["generation"])
print("Generacion reciente:", recent["generation"])
print("Archivo afectado:", candidate["name"].decode("ascii"))
print("LBA afectado:", candidate["lba"])
print("Catalogos no modificados")
print("Version antigua verificada: OK")
print("Disco descartable:", TARGET)
