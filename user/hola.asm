
bits 32

section .text
global _start

_start:
    ; Primera llamada al kernel:
    ; SYS_TEST = 1
    mov eax, 1
    int 0x80

    ; Segunda llamada:
    ; SYS_EXIT = 2
    mov eax, 2
    int 0x80

    ; Si SYS_EXIT retorna, existe un error.
    ud2
