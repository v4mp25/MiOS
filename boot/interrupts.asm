
bits 32

section .text

; Llamadas al sistema MiOS - int 0x80


global syscall_stub

extern syscall_dispatch
extern ring3_exit
extern syscall_record_origin
syscall_stub:
    ; Guardar segmentos originales
    push ds
    push es
    push fs
    push gs

    ; Guardar registros generales
    pushad
    cld

    ; Utilizar segmentos del kernel
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
; El CS original esta guardado
    ; a 52 bytes del ESP actual.
    push dword [esp + 52]
    call syscall_record_origin
    add esp, 4

    ; EAX original: numero de syscall
    mov eax, [esp + 28]
    push eax
    call syscall_dispatch
    add esp, 4

    ; ¿La llamada fue SYS_EXIT (2)?
    cmp dword [esp + 28], 2
    jne .normal_return

    ; Revisar el CPL del CS guardado.
    ;
    ; pushad:           32 bytes
    ; segmentos:        16 bytes
    ; EIP guardado:      4 bytes
    ; CS guardado:       offset 52
    mov eax, [esp + 52]
    and eax, 3

    ; SYS_EXIT desde Ring 0 conserva
    ; el comportamiento de syscall2test.
    cmp eax, 3
    jne .normal_return

    ; SYS_EXIT desde Ring 3:
    ; recuperar el contexto de shell.
    jmp ring3_exit

.normal_return:
    popad

    pop gs
    pop fs
    pop es
    pop ds

    iretd


global irq0_stub
global isr_stub_table

extern timer_irq_handler
extern exception_handler

; Excepciones sin codigo de error
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; Excepciones con codigo de error generado por CPU
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

isr_common:
    cld
    pushad

    ; Pasar la direccion del contexto guardado
    push esp
    call exception_handler
    add esp, 4

    ; No regresar a una ejecucion potencialmente dañada
    cli

.halt:
    hlt
    jmp .halt

; Temporizador original de MiOS

; IRQ0: interrupcion del temporizador.
; El controlador C devuelve el ESP
; que debemos restaurar.

global yield_stub
extern timer_irq_dispatch
extern scheduler_on_yield


irq0_stub:
    cld

    ; Al entrar, la CPU guarda:
    ; [ESP+0] = EIP
    ; [ESP+4] = CS
    ; [ESP+8] = EFLAGS
    ;
    ; Verificar el nivel de privilegio
    ; que tenia el codigo interrumpido.
    test dword [esp + 4], 3
    jz .kernel_irq
extern user_context_capture
.user_irq:
    ; Interrupcion procedente de Ring 3.
    ; Conservar segmentos de usuario.
    push ds
    push es
    push fs
    push gs
    pushad

    ; Los controladores C deben ejecutar
    ; utilizando segmentos del kernel.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Actualizar ticks y enviar EOI.
    ; TODAVIA no cambiar de tarea.
     push esp
    call user_context_capture
    add esp, 4

    ; Actualizar ticks y enviar EOI
    call timer_irq_handler
   

    ; Restaurar registros del usuario.
    popad
    pop gs
    pop fs
    pop es
    pop ds

    ; Regresar al mismo programa Ring 3.
    iretd

.kernel_irq:
    ; Mantener intacto el funcionamiento
    ; original del Round Robin del kernel.
    pushad

    push esp
    call timer_irq_dispatch
    add esp, 4

    mov esp, eax

    popad
    iretd


; Interrupcion de software para
; permitir un yield voluntario.
yield_stub:
    cld
    pushad

    push esp
    call scheduler_on_yield
    add esp, 4

    mov esp, eax

    popad
    iretd

section .rodata
align 4

isr_stub_table:
    dd isr0, isr1, isr2, isr3
    dd isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11
    dd isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19
    dd isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27
    dd isr28, isr29, isr30, isr31

