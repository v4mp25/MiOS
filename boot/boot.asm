
bits 32

section .multiboot
align 4

; Multiboot version 1
MB_MAGIC equ 0x1BADB002

; Bit 0: alinear modulos
; Bit 1: solicitar informacion de memoria
; Bit 2: solicitar modo grafico
MB_FLAGS equ 0x00000007

dd MB_MAGIC
dd MB_FLAGS
dd -(MB_MAGIC + MB_FLAGS)

; Cinco campos de direccion reservados.
; No usamos el mecanismo a.out kludge.
dd 0    ; header_addr
dd 0    ; load_addr
dd 0    ; load_end_addr
dd 0    ; bss_end_addr
dd 0    ; entry_addr

; Solicitud de modo grafico
dd 0    ; mode_type = framebuffer lineal
dd 640  ; ancho en pixeles
dd 480  ; alto en pixeles
dd 32   ; bits por pixel

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    cld

    ; Pasar información Multiboot al kernel
    push ebx
    push eax
    call kernel_main

    add esp, 8
.hang:
    cli
    hlt
    jmp .hang
