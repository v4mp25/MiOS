
bits 32

section .text

global thread_switch
global thread_enter
global thread_resume_irq

; Cambio de contexto convencional.
; Se usa para regresar al contexto principal.
thread_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp
    mov esp, [edx]

    pop edi
    pop esi
    pop ebx
    pop ebp

    ret

; void thread_enter(
;     uint32_t *main_esp,
;     uint32_t next_esp
; );
;
; Guarda el contexto principal y entra
; al primer hilo mediante IRETD.
thread_enter:
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp

    mov esp, edx

    popad
    iretd

; Restaura un hilo desde un contexto
; preparado como marco de interrupcion.
thread_resume_irq:
    mov esp, [esp + 4]
    popad
    iretd
