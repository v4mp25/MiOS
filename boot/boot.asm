bits 32

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000003
    dd -(0x1BADB002 + 0x00000003)

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
