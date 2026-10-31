
ASM = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -O0 -Wall -Wextra -Iinclude
LDFLAGS = -m elf_i386

all: os.iso

boot.o: boot/boot.asm
	$(ASM) -f elf32 $< -o $@

kernel.o: kernel/kernel.c include/fs.h include/interrupts.h include/memory.h include/thread.h include/process.h include/memory_map.h include/pmm.h include/paging.h include/irq.h include/address_space.h
	$(CC) $(CFLAGS) -c kernel/kernel.c -o kernel.o
fs.o: fs/fs.c include/fs.h
	$(CC) $(CFLAGS) -c fs/fs.c -o fs.o
memory.o: kernel/memory.c include/memory.h
	$(CC) $(CFLAGS) -c kernel/memory.c -o memory.o

address_space.o: kernel/address_space.c include/address_space.h include/paging.h include/pmm.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/address_space.c -o address_space.o



kernel.bin: boot.o kernel.o fs.o ata.o interrupts.o interrupts_c.o memory.o memory_map.o thread.o address_space.o pmm.o thread_switch.o paging.o process.o kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o kernel.bin boot.o kernel.o fs.o ata.o interrupts.o interrupts_c.o memory.o paging.o memory_map.o address_space.o pmm.o thread.o thread_switch.o process.o

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
	rm -f boot.o kernel.o fs.o ata.o interrupts.o interrupts_c.o memory.o paging.o thread.o address_space.o thread_switch.o process.o kernel.bin os.iso memory_map.o
	rm -rf iso/boot
interrupts.o: boot/interrupts.asm
	$(ASM) -f elf32 boot/interrupts.asm -o interrupts.o
process.o: kernel/process.c include/process.h include/thread.h include/address_space.h include/paging.h
	$(CC) $(CFLAGS) -c kernel/process.c -o process.o
pmm.o: kernel/pmm.c include/pmm.h include/memory_map.h
	$(CC) $(CFLAGS) -c kernel/pmm.c -o pmm.o
paging.o: kernel/paging.c include/paging.h include/pmm.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/paging.c -o paging.o
interrupts_c.o: kernel/interrupts.c include/interrupts.h
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o interrupts_c.o
memory_map.o: kernel/memory_map.c include/memory_map.h
	$(CC) $(CFLAGS) -c kernel/memory_map.c -o memory_map.o
thread.o: kernel/thread.c include/thread.h include/memory.h include/paging.h
	$(CC) $(CFLAGS) -c kernel/thread.c -o thread.o

thread_switch.o: boot/thread_switch.asm
	$(ASM) -f elf32 boot/thread_switch.asm -o thread_switch.o

.PHONY: all run clean
