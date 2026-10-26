
ASM = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -O0 -Wall -Wextra -Iinclude
LDFLAGS = -m elf_i386

all: os.iso

boot.o: boot/boot.asm
	$(ASM) -f elf32 $< -o $@

kernel.o: kernel/kernel.c include/fs.h
	$(CC) $(CFLAGS) -c kernel/kernel.c -o kernel.o

fs.o: fs/fs.c include/fs.h
	$(CC) $(CFLAGS) -c fs/fs.c -o fs.o

kernel.bin: boot.o kernel.o fs.o ata.o kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o kernel.bin boot.o kernel.o fs.o ata.o

os.iso: kernel.bin boot/grub/grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	cp boot/grub/grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o os.iso iso
ata.o: drivers/ata.c include/ata.h
	gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -O0 -Wall -Wextra -Iinclude -c drivers/ata.c -o ata.o

run: os.iso
	qemu-system-i386 \
		-drive file=mios-disk.img,format=raw,if=ide,index=0,media=disk \
		-cdrom os.iso \
		-boot order=d \
		-m 128M
clean:
	rm -f boot.o kernel.o fs.o kernel.bin os.iso ata.o
	rm -rf iso/boot

.PHONY: all run clean
