
bits 32

section .text

; Colocar 16 bytes antes del programa real.
; El punto de entrada quedara desplazado.
times 16 nop

global _start

_start:
    ; SYS_TEST
    mov eax, 1
    int 0x80

    ; SYS_EXIT
    mov eax, 2
    int 0x80

    ; No debe continuar despues de salir.
    ud2
