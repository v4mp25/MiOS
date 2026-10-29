
#include <stdint.h>
#include <stddef.h>

#include "thread.h"
#include "memory.h"

/*
 * Primera implementacion:
 * - Dos hilos del kernel.
 * - Una pagina de pila por hilo.
 * - Cambio cooperativo de contexto.
 * - No hay aislamiento entre hilos.
 */

#define THREAD_FREE 0
#define THREAD_READY 1
#define THREAD_DONE 2

typedef struct {
    uint32_t esp;
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

/* Buscar un hilo listo para ejecutar */
static int find_next(int after) {
    for (int step = 1;
         step <= MIOS_MAX_THREADS;
         step++) {

        int id = (after + step)
                 % MIOS_MAX_THREADS;

        if (threads[id].state == THREAD_READY)
            return id;
    }

    return -1;
}

/* Ejecutado cuando termina un hilo */
static void thread_finish(void)
    __attribute__((noreturn));

static void thread_finish(void) {
    int previous = current_thread;

    threads[previous].state = THREAD_DONE;

    int next = find_next(previous);

    if (next >= 0) {
        current_thread = next;

        thread_switch(
            &threads[previous].esp,
            &threads[next].esp
        );
    } else {
        current_thread = -1;

        thread_switch(
            &threads[previous].esp,
            &main_esp
        );
    }

    /* Nunca deberia regresar aqui */
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Punto de entrada inicial de cada hilo */
static void thread_entry(void) {
    int id = current_thread;

    if (id >= 0 && id < MIOS_MAX_THREADS) {
        if (threads[id].function != NULL)
            threads[id].function();
    }

    thread_finish();
}

/* Inicializar la tabla de hilos */
void thread_init(void) {
    if (scheduler_running)
        return;

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        threads[i].esp = 0;
        threads[i].function = NULL;
        threads[i].stack = NULL;
        threads[i].state = THREAD_FREE;
    }

    main_esp = 0;
    current_thread = -1;
}

/* Crear un hilo con su propia pila */
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
         * Pila inicial esperada por
         * thread_switch:
         *
         * edi, esi, ebx, ebp, retorno
         */

        *(--sp) = (uint32_t)thread_entry;
        *(--sp) = 0; /* ebp */
        *(--sp) = 0; /* ebx */
        *(--sp) = 0; /* esi */
        *(--sp) = 0; /* edi */

        threads[i].esp = (uint32_t)sp;
        threads[i].function = function;
        threads[i].stack = stack;
        threads[i].state = THREAD_READY;

        return i;
    }

    return -1;
}

/* Ceder voluntariamente la CPU */
void thread_yield(void) {
    if (!scheduler_running || current_thread < 0)
        return;

    int previous = current_thread;
    int next = find_next(previous);

    if (next < 0 || next == previous)
        return;

    current_thread = next;

    thread_switch(
        &threads[previous].esp,
        &threads[next].esp
    );
}

/* Ejecutar todos los hilos creados */
int thread_run(void) {
    if (scheduler_running)
        return -1;

    int first = find_next(-1);

    if (first < 0)
        return -1;

    scheduler_running = 1;
    current_thread = first;

    thread_switch(
        &main_esp,
        &threads[first].esp
    );

    /*
     * Regresamos cuando todos
     * los hilos finalizaron.
     */

    scheduler_running = 0;
    current_thread = -1;

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        if (threads[i].stack != NULL) {
            memory_free_page(threads[i].stack);
        }

        threads[i].stack = NULL;
        threads[i].function = NULL;
        threads[i].esp = 0;
        threads[i].state = THREAD_FREE;
    }

    return 0;
}

/* Cantidad de hilos creados */
uint32_t thread_count(void) {
    uint32_t count = 0;

    for (int i = 0; i < MIOS_MAX_THREADS; i++) {
        if (threads[i].state != THREAD_FREE)
            count++;
    }

    return count;
}
