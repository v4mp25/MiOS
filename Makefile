
ASM = nasm
CC = gcc
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -O0 -Wall -Wextra -Iinclude
LDFLAGS = -m elf_i386

all: os.iso

boot.o: boot/boot.asm
	$(ASM) -f elf32 $< -o $@

kernel.o: kernel/kernel.c include/fs.h include/interrupts.h include/memory.h include/thread.h include/process.h include/memory_map.h include/pmm.h include/paging.h include/irq.h include/address_space.h include/gdt.h include/tss.h include/syscall.h include/user_program.h include/ring3.h include/user_context.h include/user_tasks.h include/user_scheduler.h include/user_process.h include/elf32.h include/elf_disk.h include/binfs.h include/elf_exec.h include/binfs_verify.h include/binfs_mirror.h
	$(CC) $(CFLAGS) -c kernel/kernel.c -o kernel.o
fs.o: fs/fs.c include/fs.h
	$(CC) $(CFLAGS) -c fs/fs.c -o fs.o

user_copy.o: kernel/user_copy.c include/user_copy.h include/paging.h
	$(CC) $(CFLAGS) -c kernel/user_copy.c -o user_copy.o

elf32.o: kernel/elf32.c include/elf32.h
	$(CC) $(CFLAGS) -c kernel/elf32.c -o elf32.o

binfs_mirror.o: kernel/binfs_mirror.c include/binfs_mirror.h include/ata.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/binfs_mirror.c -o binfs_mirror.o


user_process.o: kernel/user_process.c include/user_process.h include/user_tasks.h include/address_space.h include/paging.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/user_process.c -o user_process.o

elf_exec.o: kernel/elf_exec.c include/elf_exec.h include/elf32.h include/binfs.h include/address_space.h include/paging.h include/ring3.h include/syscall.h include/tss.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/elf_exec.c -o elf_exec.o


binfs.o: kernel/binfs.c include/binfs.h include/binfs_mirror.h include/ata.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/binfs.c -o binfs.o



elf_loader.o: kernel/elf_loader.c include/elf32.h include/address_space.h include/paging.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/elf_loader.c -o elf_loader.o

user_tasks.o: kernel/user_tasks.c include/user_tasks.h include/address_space.h include/user_program.h include/paging.h
	$(CC) $(CFLAGS) -c kernel/user_tasks.c -o user_tasks.o

user_scheduler.o: kernel/user_scheduler.c include/user_scheduler.h include/user_tasks.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/user_scheduler.c -o user_scheduler.o


user_program.o: kernel/user_program.c include/user_program.h include/pmm.h
	$(CC) $(CFLAGS) -c kernel/user_program.c -o user_program.o
memory.o: kernel/memory.c include/memory.h
	$(CC) $(CFLAGS) -c kernel/memory.c -o memory.o

user_context.o: kernel/user_context.c include/user_context.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/user_context.c -o user_context.o

ring3.o: boot/ring3.asm
	nasm -f elf32 boot/ring3.asm -o ring3.o

address_space.o: kernel/address_space.c include/address_space.h include/paging.h include/pmm.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/address_space.c -o address_space.o
tss.o: kernel/tss.c include/tss.h include/gdt.h
	$(CC) $(CFLAGS) -c kernel/tss.c -o tss.o

elf_disk.o: kernel/elf_disk.c include/elf_disk.h include/ata.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/elf_disk.c -o elf_disk.o


kernel.bin: boot.o kernel.o fs.o ata.o interrupts.o interrupts_c.o memory.o binfs_mirror.o binfs_verify.o elf_exec.o elf_disk.o binfs.o elf_loader.o elf32.o user_process.o user_scheduler.o memory_map.o user_tasks.o ring3.o user_program.o thread.o syscall.o user_copy.o tss.o user_context.o address_space.o pmm.o thread_switch.o gdt.o hola_elf.o gdt_flush.o paging.o process.o kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o kernel.bin boot.o kernel.o binfs.o binfs_verify.o binfs_mirror.o elf_exec.o elf_disk.o user_process.o elf_loader.o elf32.o user_scheduler.o user_tasks.o fs.o ring3.o ata.o user_program.o interrupts.o user_copy.o interrupts_c.o syscall.o tss.o memory.o user_context.o gdt.o gdt_flush.o paging.o hola_elf.o memory_map.o address_space.o pmm.o thread.o thread_switch.o process.o

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
	rm -f boot.o kernel.o fs.o ata.o interrupts.o interrupts_c.o elf_exec.o binfs_mirror.o user_copy.o binfs_verify.o user_process.o binfs.o elf_disk.o elf32.o elf_loader.o user_scheduler.o user_context.o user_tasks.o memory.o ring3.o user_program.o gdt.o syscall.o tss.o gdt_flush.o paging.o thread.o address_space.o thread_switch.o process.o kernel.bin os.iso memory_map.o user/hola.o user/hola.elf hola_elf.o
	rm -rf iso/boot
interrupts.o: boot/interrupts.asm
	$(ASM) -f elf32 boot/interrupts.asm -o interrupts.o
process.o: kernel/process.c include/process.h include/thread.h include/address_space.h include/paging.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/process.c -o process.o
pmm.o: kernel/pmm.c include/pmm.h include/memory_map.h
	$(CC) $(CFLAGS) -c kernel/pmm.c -o pmm.o
paging.o: kernel/paging.c include/paging.h include/pmm.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/paging.c -o paging.o

gdt.o: kernel/gdt.c include/gdt.h include/tss.h
	$(CC) $(CFLAGS) -c kernel/gdt.c -o gdt.o

user/hola.o: user/hola.asm
	nasm -f elf32 user/hola.asm -o user/hola.o

user/hola.elf: user/hola.o user/user.ld
	$(LD) -m elf_i386 -T user/user.ld -o user/hola.elf user/hola.o

hola_elf.o: user/hola.elf
	objcopy -I binary -O elf32-i386 -B i386 user/hola.elf hola_elf.o


gdt_flush.o: boot/gdt_flush.asm
	$(ASM) -f elf32 boot/gdt_flush.asm -o gdt_flush.o

syscall.o: kernel/syscall.c include/syscall.h include/irq.h include/user_copy.h
	$(CC) $(CFLAGS) -c kernel/syscall.c -o syscall.o
interrupts_c.o: kernel/interrupts.c include/interrupts.h
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o interrupts_c.o
memory_map.o: kernel/memory_map.c include/memory_map.h
	$(CC) $(CFLAGS) -c kernel/memory_map.c -o memory_map.o
thread.o: kernel/thread.c include/thread.h include/memory.h include/irq.h include/paging.h include/tss.h
	$(CC) $(CFLAGS) -c kernel/thread.c -o thread.o

binfs_verify.o: kernel/binfs_verify.c include/binfs_verify.h include/binfs.h include/elf32.h include/irq.h
	$(CC) $(CFLAGS) -c kernel/binfs_verify.c -o binfs_verify.o


thread_switch.o: boot/thread_switch.asm
	$(ASM) -f elf32 boot/thread_switch.asm -o thread_switch.o

.PHONY: all run clean
