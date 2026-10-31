
bits 32

section .bss
align 4

ring3_saved_esp: resd 1
ring3_saved_cr3: resd 1

section .text


global ring3_enter
global ring3_enter_at
global ring3_exit

; Entrada antigua: mantiene compatibilidad
ring3_enter:
    mov edx, 0x10800000
    jmp ring3_enter_common

; Entrada nueva: EIP indicado por ELF
ring3_enter_at:
    mov edx, [esp + 12]
    jmp ring3_enter_common

; El resto del lanzador es compartido
ring3_enter_common:
    push ebp
    mov ebp, esp


    push ebx
    push esi
    push edi
    pushfd

    ; Conservar pila del kernel
    mov [ring3_saved_esp], esp

    ; Guardar CR3 original
    mov eax, cr3
    mov [ring3_saved_cr3], eax

    ; No permitir IRQ durante la prueba
    cli

    ; Activar espacio de usuario
    mov eax, [ebp + 8]
    mov cr3, eax

    ; Preparar marco de IRETD:
    ; SS, ESP, EFLAGS, CS, EIP
    push dword 0x23
    push dword [ebp + 12]
    push dword 0x00000202
    push dword 0x1B
    push edx

    ; Segmentos de datos de usuario
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Entrar realmente a CPL 3
    iretd


ring3_exit:
    ; Se entra aqui desde el controlador
    ; de SYS_EXIT, estando en Ring 0.
    cli

    ; Restaurar segmentos del kernel
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Volver al directorio de la shell
    mov eax, [ring3_saved_cr3]
    mov cr3, eax

    ; Recuperar pila original del kernel
    mov esp, [ring3_saved_esp]

    ; Restaurar contexto
    popfd
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; Regresar al C que llamo ring3_enter
    ret
