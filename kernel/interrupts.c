
#include <stdint.h>
#include "interrupts.h"

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

uint32_t timer_get_ticks(void) {
    return timer_ticks;
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

    idt_set_gate(
        32,
        (uint32_t)irq0_stub,
        read_cs()
    );

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint32_t)&idt;

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idt_pointer)
    );

    pic_remap();
    pit_init();

    __asm__ volatile ("sti");
}
