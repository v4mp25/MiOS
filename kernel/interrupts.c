
#include <stdint.h>
#include "interrupts.h"
#include "thread.h"
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define TIMER_HZ 100

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t attributes;
    uint16_t offset_high;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_pointer_t;

static idt_entry_t idt[256];
static idt_pointer_t idt_pointer;

static volatile uint32_t timer_ticks = 0;

extern void irq0_stub(void);
extern void yield_stub(void);
extern void (*isr_stub_table[32])(void);

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint16_t read_cs(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static void io_wait(void) {
    outb(0x80, 0);
}

static void idt_set_gate(
    uint8_t number,
    uint32_t handler,
    uint16_t selector
) {
    idt[number].offset_low = handler & 0xFFFF;
    idt[number].selector = selector;
    idt[number].zero = 0;
    idt[number].attributes = 0x8E;
    idt[number].offset_high = handler >> 16;
}

static void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Habilitamos solo IRQ0: temporizador */
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}

static void pit_init(void) {
    uint16_t divisor = (uint16_t)(1193182 / TIMER_HZ);

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

/* Se llama desde interrupts.asm */
void timer_irq_handler(void) {
    timer_ticks++;

    /* Avisar al PIC que terminamos de atender IRQ0 */
    outb(PIC1_COMMAND, 0x20);
}

uint32_t timer_irq_dispatch(uint32_t esp) {
    /* Actualiza ticks y notifica al PIC */
    timer_irq_handler();

    /* Selecciona el contexto que continuara */
    return scheduler_on_tick(esp);
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

/* Diagnostico de excepciones del procesador */

static volatile uint16_t *const ERROR_VGA =
    (volatile uint16_t *)0xB8000;

static uint32_t error_cursor = 0;

static void error_putc(char c) {
    if (c == '\n') {
        error_cursor =
            ((error_cursor / 80) + 1) * 80;
        return;
    }

    if (error_cursor < 80 * 25) {
        ERROR_VGA[error_cursor++] =
            ((uint16_t)0x0F << 8) |
            (uint8_t)c;
    }
}

static void error_print(const char *text) {
    while (*text) {
        error_putc(*text++);
    }
}

static void error_hex(uint32_t value) {
    const char *digits = "0123456789ABCDEF";

    error_print("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        error_putc(digits[(value >> shift) & 15]);
    }
}

void exception_handler(const uint32_t *frame) {
    __asm__ volatile ("cli");

    uint32_t vector = frame[8];
    uint32_t error_code = frame[9];
    uint32_t eip = frame[10];

    for (uint32_t i = 0; i < 80 * 25; i++) {
        ERROR_VGA[i] = 0x0F20;
    }

    error_cursor = 0;

    error_print("================================\n");
    error_print("MIOS - ERROR DEL PROCESADOR\n");
    error_print("================================\n\n");

    error_print("Vector: ");
    error_hex(vector);
    error_print("\n");

    error_print("Codigo de error: ");
    error_hex(error_code);
    error_print("\n");

    error_print("Direccion EIP: ");
    error_hex(eip);
    error_print("\n\n");

    switch (vector) {
        case 0:
            error_print("Division entre cero\n");
            break;
        case 3:
            error_print("Punto de interrupcion (breakpoint)\n");
            break;
        case 6:
            error_print("Instruccion invalida\n");
            break;

            case 8:
            error_print("Double fault\n");
            break;
        case 13:
            error_print("Fallo de proteccion general\n");
            break;
        
case 14: {
    uint32_t cr2;

    /* CR2 contiene la direccion que provoco el fallo */
    __asm__ volatile (
        "mov %%cr2, %0"
        : "=r"(cr2)
    );

    error_print("PAGE FAULT - FALLO DE PAGINA\n");

    error_print("Direccion virtual: ");
    error_hex(cr2);
    error_print("\n");

    error_print("Causa: ");

    if (error_code & 0x01)
        error_print("Violacion de proteccion\n");
    else
        error_print("Pagina no presente\n");

    error_print("Operacion: ");

    if (error_code & 0x02)
        error_print("Escritura\n");
    else
        error_print("Lectura\n");

    error_print("Modo: ");

    if (error_code & 0x04)
        error_print("Usuario\n");
    else
        error_print("Kernel\n");

    if (error_code & 0x08)
        error_print("Bit reservado invalido\n");

    if (error_code & 0x10)
        error_print("Fallo durante busqueda de instruccion\n");

    break;
}

        default:
            error_print("Excepcion del procesador\n");
            break;
    }

    error_print("\nSistema detenido por seguridad.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void interrupts_init(void) {
    __asm__ volatile ("cli");

    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].attributes = 0;
        idt[i].offset_high = 0;
    }
/* Registrar excepciones de CPU: vectores 0-31 */
uint16_t selector = read_cs();

for (uint32_t i = 0; i < 32; i++) {
    idt_set_gate(
        (uint8_t)i,
        (uint32_t)isr_stub_table[i],
        selector
    );
}
    idt_set_gate(
        32,
        (uint32_t)irq0_stub,
        read_cs()
    );

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint32_t)&idt;
idt_set_gate(
    48,
    (uint32_t)yield_stub,
    read_cs()
);
    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idt_pointer)
    );

    pic_remap();
    pit_init();

    __asm__ volatile ("sti");
}
