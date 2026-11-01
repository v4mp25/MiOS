
from pathlib import Path

DISK = Path("mios-disk-mirror-test.img")

SECTOR_SIZE = 512
PRIMARY_LBA = 512
MIRROR_LBA = 600

MAGIC = b"MIOSBIN1"

if not DISK.is_file():
    raise SystemExit("ERROR: disco no encontrado")

with DISK.open("r+b") as disk:

    disk.seek(0, 2)
    disk_size = disk.tell()

    required = (MIRROR_LBA + 1) * SECTOR_SIZE

    if disk_size < required:
        raise SystemExit("ERROR: disco insuficiente")

    # Leer catalogo original
    disk.seek(PRIMARY_LBA * SECTOR_SIZE)
    primary = disk.read(SECTOR_SIZE)

    if len(primary) != SECTOR_SIZE:
        raise SystemExit("ERROR: lectura incompleta")

    if primary[:8] != MAGIC:
        raise SystemExit("ERROR: firma BINFS invalida")

    count = int.from_bytes(primary[8:12], "little")

    if count < 1 or count > 4:
        raise SystemExit("ERROR: numero de slots invalido")

    # Comprobar destino
    disk.seek(MIRROR_LBA * SECTOR_SIZE)
    previous = disk.read(SECTOR_SIZE)

    if len(previous) != SECTOR_SIZE:
        raise SystemExit("ERROR: sector destino incompleto")

    if any(previous):
        raise SystemExit(
            "ERROR: sector 600 no esta vacio. "
            "No se modifico el disco."
        )

    # Crear copia redundante
    disk.seek(MIRROR_LBA * SECTOR_SIZE)
    disk.write(primary)
    disk.flush()

    # Verificar la escritura
    disk.seek(MIRROR_LBA * SECTOR_SIZE)
    copied = disk.read(SECTOR_SIZE)

    if copied != primary:
        raise SystemExit("ERROR: copia no coincide")

print("CATALOGO SECUNDARIO CREADO")
print("Catalogo principal: LBA", PRIMARY_LBA)
print("Catalogo secundario: LBA", MIRROR_LBA)
print("Slots registrados:", count)
print("Comparacion de 512 bytes: OK")
