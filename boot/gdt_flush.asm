
bits 32

section .text

global gdt_load

gdt_load:
    ; Recibir puntero a la GDT
    mov eax, [esp + 4]

    ; Mantener IRQ deshabilitadas
    cli

    ; Instalar la GDT
    lgdt [eax]

    ; Recargar CS mediante salto lejano
    jmp 0x08:.reload_cs

.reload_cs:
    ; Recargar selectores de datos
    mov ax, 0x10

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ret
