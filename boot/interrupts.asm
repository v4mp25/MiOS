
bits 32

section .text

global irq0_stub
extern timer_irq_handler

irq0_stub:
    cld
    pushad

    call timer_irq_handler

    popad
    iretd

