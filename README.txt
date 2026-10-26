MiOS v0.7 - comandos tree, rm y rmdir

Este paquete contiene archivos completos para reemplazar:
  kernel/kernel.c
  fs/fs.c
  include/fs.h

include/ata.h se incluye como referencia; debe conservar las declaraciones
que ya usa el controlador ATA instalado.

Integracion:
1. Cierra QEMU.
2. Haz copia de seguridad de kernel/kernel.c, fs/fs.c e include/fs.h.
3. Extrae estos archivos dentro de ~/MiOS respetando las carpetas.
4. Conserva tu drivers/ata.c, include/ata.h, Makefile y mios_disk.img actuales.
5. El Makefile debe compilar fs/fs.c a fs.o y enlazar fs.o y ata.o en kernel.bin.
6. Ejecuta make clean && make; verifica con grub-file --is-x86-multiboot kernel.bin.

Comandos nuevos:
  tree [RUTA]  lista recursivamente el contenido de un directorio
  rm RUTA      elimina un archivo, no un directorio
  rmdir RUTA   elimina solo directorios vacios; protege raiz y cwd

El formato persistente existente se conserva: superblock LBA 101 y tabla
serializada desde LBA 102. LBA 100 sigue reservado para disktest.
Los cambios de eliminacion se guardan en el disco mediante ATA. Este es un
filesystem educativo limitado; realiza una copia de seguridad del disco antes
de actualizar y no lo uses para datos importantes.
