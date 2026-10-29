
bits 32

section .text

global thread_switch

; void thread_switch(
;     uint32_t *old_esp,
;     uint32_t *new_esp
; );

thread_switch:
    ; Obtener los argumentos antes de
    ; modificar la pila.
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    ; Guardar registros preservados por
    ; la convencion de llamadas de C.
    push ebp
    push ebx
    push esi
    push edi

    ; Guardar la pila del hilo anterior.
    mov [eax], esp

    ; Restaurar la pila del siguiente.
    mov esp, [edx]

    ; Restaurar sus registros.
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; Continuar su ejecucion.
    ret
