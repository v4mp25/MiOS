
#include <stdint.h>
#include <stddef.h>
#include "thread.h"
#include "memory.h"
#include "irq.h"
#include "paging.h"
#define THREAD_FREE  0
#define THREAD_READY 1
#define THREAD_DONE  2

#define QUANTUM_TICKS 5U

typedef struct {
    uint32_t esp;
    uint32_t cr3;
    void (*function)(void);
    void *stack;
    int state;
} Thread;

static Thread threads[MIOS_MAX_THREADS];

static uint32_t main_esp = 0;
static int current_thread = -1;
static int scheduler_running = 0;

extern void thread_switch(
    uint32_t *old_esp,
    uint32_t *new_esp
);

extern void thread_enter(
    uint32_t *main_esp,
    uint32_t next_esp
);

extern void thread_resume_irq(uint32_t esp)
    __attribute__((noreturn));

static inline void irq_disable(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static inline void irq_enable(void) {
    __asm__ volatile ("sti" ::: "memory");
}

/* Consulta el selector de codigo actual */
static uint16_t read_cs(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

/* Buscar el siguiente hilo listo */
static int find_next(int after) {
    for (int step = 1; step <= MIOS_MAX_THREADS; step++) {
        int id = (after + step) % MIOS_MAX_THREADS;

        if (threads[id].state == THREAD_READY)
            return id;
    }
    return -1;
}

static void thread_finish(void)
    __attribute__((noreturn));

static void thread_finish(void) {
    irq_disable();

    int previous = current_thread;

    threads[previous].state = THREAD_DONE;

    int next = find_next(previous);

    if (next >= 0) {
        current_thread = next;
        thread_resume_irq(threads[next].esp);
    }

    /* Todos terminaron: regresar al shell */
    current_thread = -1;

    thread_switch(
        &threads[previous].esp,
        &main_esp
    );

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* Primera funcion que ejecuta cada hilo */
static void thread_entry(void) {
    int id = current_thread;

    if (id >= 0 && id < MIOS_MAX_THREADS) {
        if (threads[id].function != NULL)
            threads[id].function();
    }

    thread_finish();
}

void thread_init(void) {
    uint32_t flags;

    __asm__ volatile (
        "pushfl; popl %0; cli"
        : "=r"(flags)
        :
        : "memory"
    );

    if (!scheduler_running) {
        for (int i = 0; i < MIOS_MAX_THREADS; i++) {
            threads[i].esp = 0;
            threads[i].cr3 = 0;
            threads[i].function = NULL;
            threads[i].stack = NULL;
            threads[i].state = THREAD_FREE;
        }

        main_esp = 0;
        current_thread = -1;
    }

    if (flags & (1U << 9)) {
        irq_enable();
    }
}

/* Crear un hilo con contexto inicial de IRQ */
int thread_create(void (*function)(void)) {
    if (function == NULL || scheduler_running)
        return -1;

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        if (threads[i].state != THREAD_FREE)
            continue;

        void *stack = memory_alloc_page();

        if (stack == NULL)
            return -1;

        uintptr_t top =
            (uintptr_t)stack + MIOS_PAGE_SIZE;

        uint32_t *sp = (uint32_t *)top;

        /*
         * Marco inicial para POPAD + IRETD.
         * IRETD consume EIP, CS y EFLAGS.
         */

        *(--sp) = 0x00000202U;          /* EFLAGS, IF=1 */
        *(--sp) = (uint32_t)read_cs();  /* CS */
        *(--sp) = (uint32_t)thread_entry; /* EIP */

        /* Registros guardados por PUSHAD */
        *(--sp) = 0; /* EAX */
        *(--sp) = 0; /* ECX */
        *(--sp) = 0; /* EDX */
        *(--sp) = 0; /* EBX */
        *(--sp) = 0; /* ESP original, POPAD lo ignora */
        *(--sp) = 0; /* EBP */
        *(--sp) = 0; /* ESI */
        *(--sp) = 0; /* EDI */

        threads[i].esp = (uint32_t)sp;

/* Por defecto, utilizar el espacio del kernel */
threads[i].cr3 = paging_cpu_enabled()
    ? paging_directory_address()
    : 0;

threads[i].function = function;
        threads[i].stack = stack;
        threads[i].state = THREAD_READY;

        return i;
    }

    return -1;
}

/*
 * Selecciona otro contexto.
 * Se llama con interrupciones deshabilitadas,
 * desde IRQ0 o desde la interrupcion de yield.
 */
static uint32_t schedule_from_irq(uint32_t esp) {
    if (!scheduler_running || current_thread < 0)
        return esp;

    int previous = current_thread;

    int next = find_next(previous);

    if (next < 0 || next == previous)
        return esp;

    /* Conservar el contexto interrumpido */
    threads[previous].esp = esp;

    current_thread = next;

    /* El ASM restaurara este nuevo contexto */
    return threads[next].esp;
}

/* Llamado desde el temporizador */
uint32_t scheduler_on_tick(uint32_t esp) {
    static uint32_t quantum_count = 0;

    if (!scheduler_running) {
        quantum_count = 0;
        return esp;
    }

    quantum_count++;

    if (quantum_count < QUANTUM_TICKS)
        return esp;

    quantum_count = 0;

    return schedule_from_irq(esp);
}

/* Cambio solicitado voluntariamente */
uint32_t scheduler_on_yield(uint32_t esp) {
    return schedule_from_irq(esp);
}

void thread_yield(void) {
    if (scheduler_running) {
        __asm__ volatile ("int $0x30" ::: "memory");
    }
}

/* Ejecutar los hilos */
int thread_run(void) {
    if (scheduler_running)
        return -1;

    irq_disable();

    int first = find_next(-1);

    if (first < 0) {
        irq_enable();
        return -1;
    }

    scheduler_running = 1;
    current_thread = first;

    /* Regresa cuando todos los hilos terminan */
    thread_enter(
        &main_esp,
        threads[first].esp
    );

    /* Estamos nuevamente en el contexto principal */
    scheduler_running = 0;
    current_thread = -1;

    irq_enable();

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        if (threads[i].stack != NULL)
            memory_free_page(threads[i].stack);

        threads[i].stack = NULL;
        threads[i].function = NULL;
        threads[i].cr3 = 0;
        threads[i].esp = 0;
        threads[i].state = THREAD_FREE;
    }

    return 0;
}

uint32_t thread_count(void) {
    uint32_t count = 0;

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        if (threads[i].state != THREAD_FREE)
            count++;
    }

    return count;
}

/*
 * Cancela un hilo pendiente de ejecucion.
 *
 * No permite destruir hilos mientras
 * el planificador esta ejecutandose.
 */
int thread_destroy(int tid) {
    uint32_t flags = irq_save();

    if (tid < 0 ||
        tid >= MIOS_MAX_THREADS ||
        scheduler_running ||
        threads[tid].state != THREAD_READY) {

        irq_restore(flags);
        return -1;
    }

    void *stack = threads[tid].stack;

    if (stack == NULL) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Si la liberacion falla, conservamos
     * el descriptor para no perder la pila.
     */
    if (memory_free_page(stack) != 0) {
        irq_restore(flags);
        return -1;
    }

    threads[tid].esp = 0;
    
    threads[tid].function = NULL;
    threads[tid].stack = NULL;
    threads[tid].cr3 = 0;
    threads[tid].state = THREAD_FREE;

    irq_restore(flags);
    return 0;
}

/*
 * Asociar un directorio de paginas
 * a un hilo que todavia no ha comenzado.
 *
 * Esta funcion no cambia CR3 en la CPU.
 */
int thread_bind_cr3(int tid, uint32_t cr3) {
    uint32_t flags = irq_save();

    if (tid < 0 ||
        tid >= MIOS_MAX_THREADS ||
        scheduler_running ||
        threads[tid].state != THREAD_READY ||
        !paging_cpu_enabled() ||
        cr3 == 0 ||
        (cr3 & 0xFFFU) != 0) {

        irq_restore(flags);
        return -1;
    }

    threads[tid].cr3 = cr3;

    irq_restore(flags);
    return 0;
}

/*
 * Consultar el CR3 registrado.
 *
 * No necesariamente es el CR3 activo.
 */
int thread_get_cr3(int tid, uint32_t *out) {
    uint32_t flags = irq_save();

    if (tid < 0 ||
        tid >= MIOS_MAX_THREADS ||
        out == NULL ||
        threads[tid].state == THREAD_FREE) {

        irq_restore(flags);
        return -1;
    }

    *out = threads[tid].cr3;

    irq_restore(flags);
    return 0;
}
