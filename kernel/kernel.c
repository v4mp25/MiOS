#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "interrupts.h"
#include "memory.h"
#include "thread.h"
#include "memory_map.h"
#include "process.h"
#include "pmm.h"
#include "paging.h"
#include "irq.h"
#include "address_space.h"
#include "gdt.h"
#include "tss.h"
/* Limites del kernel definidos en linker.ld */
extern char __kernel_start[];
extern char __kernel_end[];

/* Ubicacion del mapa Multiboot */
extern uint32_t mios_mmap_start;
extern uint32_t mios_mmap_length;
static void imprimir_numero(uint32_t numero);
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define BUFFER_SIZE 128
#define OUTPUT_SIZE 4096
#define PATH_SIZE 128

static volatile uint16_t *const VGA = (volatile uint16_t *)0xB8000;
static size_t fila = 0, columna = 0;
static uint8_t color = 0x0F;
static uint16_t cwd = 0;
static uint32_t mbi_saved_address = 0;
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static int iguales(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void limpiar_pantalla(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA[i] = ((uint16_t)color << 8) | ' ';
    fila = columna = 0;
}
static void desplazar_pantalla(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA[(y - 1) * VGA_WIDTH + x] = VGA[y * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = ((uint16_t)color << 8) | ' ';
    fila = VGA_HEIGHT - 1;
}
static void poner_caracter(char c) {
    if (c == '\n') { columna = 0; fila++; }
    else if (c == '\b') {
        if (columna > 0) columna--;
        else if (fila > 0) { fila--; columna = VGA_WIDTH - 1; }
        else return;
        VGA[fila * VGA_WIDTH + columna] = ((uint16_t)color << 8) | ' ';
    } else {
        if (columna >= VGA_WIDTH) { columna = 0; fila++; }
        if (fila >= VGA_HEIGHT) desplazar_pantalla();
        VGA[fila * VGA_WIDTH + columna] = ((uint16_t)color << 8) | (uint8_t)c;
        columna++;
    }
    if (fila >= VGA_HEIGHT) desplazar_pantalla();
}
static void imprimir(const char *s) { while (*s) poner_caracter(*s++); }

static void error_fs(int result) {
    color = 0x0C;
    switch (result) {
        case MIOS_FS_NOT_FOUND: imprimir("Error: ruta no encontrada.\n"); break;
        case MIOS_FS_EXISTS: imprimir("Error: el elemento ya existe.\n"); break;
        case MIOS_FS_NOT_DIR: imprimir("Error: no es un directorio.\n"); break;
        case MIOS_FS_IS_DIR: imprimir("Error: es un directorio.\n"); break;
        case MIOS_FS_FULL: imprimir("Error: sistema de archivos lleno.\n"); break;
        case MIOS_FS_BUFFER: imprimir("Error: contenido demasiado grande.\n"); break;
        case MIOS_FS_NOT_EMPTY: imprimir("Error: el directorio no esta vacio.\n"); break;
        case MIOS_FS_IO: imprimir("Error: fallo de lectura/escritura del disco.\n"); break;
        default: imprimir("Error: ruta no valida.\n"); break;
    }
    color = 0x0F;
}
static void mostrar_prompt(void) {
    char path[PATH_SIZE];
    color = 0x0A;
    if (mios_fs_pwd(cwd, path, sizeof(path)) == MIOS_FS_OK) {
        imprimir("MiOS:"); imprimir(path); imprimir("> ");
    } else imprimir("MiOS> ");
    color = 0x0F;
}

static void reiniciar(void) {
    imprimir("\nReiniciando MiOS...\n");

    /* Deshabilitar interrupciones enmascarables. */
    __asm__ volatile ("cli");

    /*
     * Primer intento:
     * solicitar el reinicio mediante el controlador 8042.
     * El bit 1 del puerto 0x64 indica si su buffer de entrada
     * esta ocupado.
     */
    for (volatile uint32_t i = 0; i < 1000000U; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            outb(0x64, 0xFE);
            break;
        }
    }

    /*
     * Segundo intento:
     * provocar un triple fault si el controlador no reinicia
     * la CPU. Es una tecnica de bajo nivel para x86.
     */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } idtr = { 0, 0 };

    __asm__ volatile (
        "lidt %0\n\t"
        "int3"
        :
        : "m"(idtr)
        : "memory"
    );

    /* Salvaguarda: no continuar ejecutando el kernel. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void tarea_hilo_1(void) {
    for (int i = 1; i <= 3; i++) {
        imprimir("Hilo 1: trabajando (");
        imprimir_numero((uint32_t)i);
        imprimir(")\n");

        thread_yield();
    }
}

static void tarea_hilo_2(void) {
    for (int i = 1; i <= 3; i++) {
        imprimir("Hilo 2: trabajando (");
        imprimir_numero((uint32_t)i);
        imprimir(")\n");

        thread_yield();
    }
}

/* Prueba de multitarea apropiativa */
static volatile uint32_t prueba_hilo2_ejecutado = 0;
static volatile uint32_t prueba_hilo1_detecto_hilo2 = 0;

static void sched_tarea1(void) {
    uint32_t inicio = timer_get_ticks();

    /*
     * No llamamos a thread_yield().
     * Esperamos hasta que el hilo 2 ejecute.
     * Limite de 2 segundos para evitar
     * bloquear indefinidamente la prueba.
     */
    while (!prueba_hilo2_ejecutado &&
           (uint32_t)(timer_get_ticks() - inicio) < 200U) {
        __asm__ volatile ("" ::: "memory");
    }

    if (prueba_hilo2_ejecutado)
        prueba_hilo1_detecto_hilo2 = 1;
}

static void sched_tarea2(void) {
    prueba_hilo2_ejecutado = 1;
}

static volatile uint32_t proceso_b_ejecuto = 0;
static volatile uint32_t proceso_a_observo_b = 0;

static void tarea_proceso_a(void) {
    uint32_t inicio = timer_get_ticks();

    /* Espera sin llamar a thread_yield */
    while (!proceso_b_ejecuto &&
           (uint32_t)(timer_get_ticks() - inicio) < 200U) {
        __asm__ volatile ("" ::: "memory");
    }

    if (proceso_b_ejecuto)
        proceso_a_observo_b = 1;
}

static void tarea_proceso_b(void) {
    proceso_b_ejecuto = 1;
}

static void imprimir_hex32(uint32_t numero) {
    const char *hex = "0123456789ABCDEF";

    imprimir("0x");

    for (int i = 7; i >= 0; i--) {
        uint32_t valor =
            (numero >> (i * 4)) & 0xF;

        poner_caracter(hex[valor]);
    }
}

/*
 * MiOS - Prueba de concurrencia PMM.
 *
 * Dos hilos solicitan y liberan marcos.
 * No llaman a thread_yield().
 */

#define PMM_CONCURRENT_LOOPS 16U

static volatile uint32_t concurrent_live[2] = {0, 0};
static volatile uint32_t concurrent_done[2] = {0, 0};
static volatile uint32_t concurrent_errors = 0;
static volatile uint32_t concurrent_overlap = 0;

static void pmm_concurrent_worker(uint32_t id) {
    uint32_t other = 1U - id;

    for (uint32_t i = 0;
         i < PMM_CONCURRENT_LOOPS;
         i++) {

        uint32_t frame = 0;

        if (pmm_alloc_frame(&frame) != 0) {
            concurrent_errors++;
            return;
        }

        /*
         * Publicar la reserva y comprobar
         * que el otro hilo no tiene el
         * mismo marco.
         */
        uint32_t flags = irq_save();

        if (concurrent_live[other] == frame) {
            concurrent_errors++;
        }

        concurrent_live[id] = frame;

        if (concurrent_live[other] != 0) {
            concurrent_overlap++;
        }

        irq_restore(flags);

        /*
         * Mantener el marco reservado
         * durante 6 ticks.
         *
         * El PIT debe poder interrumpirnos.
         */
        uint32_t start = timer_get_ticks();

        while ((uint32_t)(timer_get_ticks() - start) < 6U) {
            __asm__ volatile ("" ::: "memory");
        }

        /*
         * Retirar la reserva publicada
         * y liberar el marco como una
         * unica seccion critica.
         */
        flags = irq_save();

        if (concurrent_live[id] != frame) {
            concurrent_errors++;
        }

        concurrent_live[id] = 0;

        if (pmm_free_frame(frame) != 0) {
            concurrent_errors++;
        }

        irq_restore(flags);

        concurrent_done[id]++;
    }
}

static void pmm_concurrent_task1(void) {
    pmm_concurrent_worker(0);
}

static void pmm_concurrent_task2(void) {
    pmm_concurrent_worker(1);
}

/* MiOS - Prueba concurrente del VMM */

#define VMM_CONCURRENT_LOOPS 8U

static volatile uint32_t vmm_done[2] = {0, 0};
static volatile uint32_t vmm_errors[2] = {0, 0};
static volatile uint32_t vmm_active[2] = {0, 0};
static volatile uint32_t vmm_overlap = 0;

static void vmm_concurrent_worker(uint32_t id) {
    uint32_t other = 1U - id;

    for (uint32_t i = 0; i < VMM_CONCURRENT_LOOPS; i++) {
        uint32_t frame = 0;

        if (pmm_alloc_frame(&frame) != 0) {
            vmm_errors[id]++;
            return;
        }

        if (vmm_map_page(id, frame) != 0) {
            vmm_errors[id]++;
            pmm_free_frame(frame);
            return;
        }

        volatile uint32_t *ptr =
            (volatile uint32_t *)(uintptr_t)
            (MIOS_VMM_BASE + id * PAGE_SIZE);

        uint32_t expected = 10000U * (id + 1U) + i;

        /* Escritura real a traves de la MMU */
        ptr[0] = expected;
        ptr[1023] = ~expected;

        vmm_active[id] = 1;

        uint32_t start = timer_get_ticks();

        /* Mantener la pagina activa durante 6 ticks */
        while ((uint32_t)(timer_get_ticks() - start) < 6U) {
            if (vmm_active[other])
                vmm_overlap = 1;

            __asm__ volatile ("" ::: "memory");
        }

        if (ptr[0] != expected ||
            ptr[1023] != ~expected) {
            vmm_errors[id]++;
        }

        vmm_active[id] = 0;

        if (vmm_unmap_page(id) != 0) {
            vmm_errors[id]++;
            /* No liberar un marco cuyo mapeo
               no pudo retirarse. */
            return;
        }

        if (pmm_free_frame(frame) != 0) {
            vmm_errors[id]++;
            return;
        }

        vmm_done[id]++;
    }
}

static void vmm_concurrent_task1(void) {
    vmm_concurrent_worker(0);
}

static void vmm_concurrent_task2(void) {
    vmm_concurrent_worker(1);
}

/* Prueba real de CR3 durante Round Robin */

static volatile uint32_t proc_cr3_result[2] = {0, 0};
static volatile uint32_t proc_cr3_errors[2] = {0, 0};

static void process_cr3_worker(uint32_t id) {
    uint32_t expected = (id == 0) ? 1111U : 2222U;

    volatile uint32_t *private_mem =
        (volatile uint32_t *)(uintptr_t)
        MIOS_AS_PRIVATE_VADDR;

    /* Escribir en la pagina privada */
    private_mem[0] = expected;

    uint32_t start = timer_get_ticks();

    /* Permitir varios cambios de contexto */
    while ((uint32_t)(timer_get_ticks() - start) < 30U) {
        if (private_mem[0] != expected)
            proc_cr3_errors[id]++;

        __asm__ volatile ("" ::: "memory");
    }

    proc_cr3_result[id] = private_mem[0];
}

static void process_cr3_task_a(void) {
    process_cr3_worker(0);
}

static void process_cr3_task_b(void) {
    process_cr3_worker(1);
}
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} GdtRegister;

static void mostrar_gdt(void) {
    GdtRegister gdtr;

    uint16_t cs;
    uint16_t ds;
    uint16_t ss;
    uint16_t tr;

    __asm__ volatile (
        "sgdt %0"
        : "=m"(gdtr)
    );

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    __asm__ volatile (
        "mov %%ds, %0"
        : "=r"(ds)
    );

    __asm__ volatile (
        "mov %%ss, %0"
        : "=r"(ss)
    );

    __asm__ volatile (
        "str %0"
        : "=r"(tr)
    );

    imprimir("=== DIAGNOSTICO GDT MIOS ===\n");

    imprimir("Direccion GDT: ");
    imprimir_hex32(gdtr.base);
    imprimir("\n");

    imprimir("Limite GDT: ");
    imprimir_numero(gdtr.limit);
    imprimir("\n");

    imprimir("CS: ");
    imprimir_hex32(cs);
    imprimir("\n");

    imprimir("DS: ");
    imprimir_hex32(ds);
    imprimir("\n");

    imprimir("SS: ");
    imprimir_hex32(ss);
    imprimir("\n");

    imprimir("TR: ");
    imprimir_hex32(tr);
    imprimir("\n");

    imprimir("Nivel privilegio actual: ");
    imprimir_numero(cs & 3U);
    imprimir("\n");
}

static volatile uint32_t tss_test_ok[2];

static void tss_test_task1(void) {
    tss_test_ok[0] = thread_tss_stack_ok() ? 1U : 0U;
    thread_yield();

    if (!thread_tss_stack_ok())
        tss_test_ok[0] = 0;
}

static void tss_test_task2(void) {
    tss_test_ok[1] = thread_tss_stack_ok() ? 1U : 0U;
    thread_yield();

    if (!thread_tss_stack_ok())
        tss_test_ok[1] = 0;
}

static void ejecutar_comando(char *linea) {
    char *args = linea;
    while (*args && *args != ' ') args++;
    if (*args) { *args++ = '\0'; while (*args == ' ') args++; }

    char salida[OUTPUT_SIZE];
    char path[PATH_SIZE];
    int result;

  
if (iguales(linea, "")) return;
else if (iguales(linea, "help")) {
    imprimir("Comandos de MiOS:\n");
    imprimir("help, clear, about, version, echo TEXTO\n");
    imprimir("pwd, cd RUTA, ls [RUTA], tree [RUTA]\n");
    imprimir("mkdir RUTA, touch RUTA, cat RUTA\n");
    imprimir("write RUTA TEXTO, rm RUTA, rmdir RUTA\n");
    imprimir("reboot, ticks\n");
    imprimir("meminfo, memtest\n");
    imprimir("testexception\n");
    imprimir("threadtest\n");
    imprimir("schedtest\n");
    imprimir("ps, proctest\n");
    imprimir("uptime\n");
    imprimir("memmap\n");
    imprimir("memdebug\n");
    imprimir("bootinfo\n");
    imprimir("pmminfo, pmmtest\n");
    imprimir("pmmstress\n");
    imprimir("pmmguard\n");
    imprimir("pmmvalidate\n");
    imprimir("bootrefs\n");
    imprimir("bootvalidate\n");
    imprimir("paginginfo\n");
    imprimir("vmmtest\n");
    imprimir("pagingon, pagingstatus\n");
    imprimir("vmmmulti\n");
    imprimir("vmmguard\n");
    imprimir("vmmreadonly\n");
    imprimir("vmmwritetest\n");
    imprimir("pmmownership\n");
    imprimir("irqtest\n");
    imprimir("pmmconcurrent\n");
    imprimir("threadcleanup\n");
    imprimir("vmmconcurrent\n");
    imprimir("spacetest\n");
    imprimir("cr3test\n");
    imprimir("ascreate, aslist, asdestroy ID\n");
    imprimir("asbusytest\n");
    imprimir("processcr3test\n");
    imprimir("procclean\n");
    imprimir("gdtinfo\n");
    imprimir("gdtcheck\n");
    imprimir("tsscheck\n");
    imprimir("tssstacktest\n");
} else if (iguales(linea, "clear")) {
    limpiar_pantalla();
}

    else if (iguales(linea, "about")) {
        imprimir("MiOS: sistema operativo educativo.\n");
        imprimir("Arquitectura: x86 de 32 bits.\nSistema de archivos: persistente en disco ATA.\n");
    } else if (iguales(linea, "version")) imprimir("MiOS version 0.7\n");
    else if (iguales(linea, "echo")) { imprimir(args); imprimir("\n"); }
    else if (iguales(linea, "pwd")) {
        result = mios_fs_pwd(cwd, path, sizeof(path));
        if (result == MIOS_FS_OK) { imprimir(path); imprimir("\n"); }
        else error_fs(result);
    } else if (iguales(linea, "cd")) {
        if (!args[0]) cwd = 0;
        else { result = mios_fs_cd(cwd, args); if (result >= 0) cwd = (uint16_t)result; else error_fs(result); }
    } else if (iguales(linea, "ls")) {
        result = mios_fs_ls(cwd, args[0] ? args : ".", salida, sizeof(salida));
        if (result == MIOS_FS_OK) { if (salida[0]) imprimir(salida); else imprimir("(directorio vacio)\n"); }
        else error_fs(result);
    } else if (iguales(linea, "mkdir")) {
        if (!args[0]) imprimir("Uso: mkdir RUTA\n");
        else { result = mios_fs_mkdir(cwd, args); if (result == MIOS_FS_OK) imprimir("Directorio creado.\n"); else error_fs(result); }
    } else if (iguales(linea, "touch")) {
        if (!args[0]) imprimir("Uso: touch RUTA\n");
        else { result = mios_fs_touch(cwd, args); if (result == MIOS_FS_OK) imprimir("Archivo creado.\n"); else error_fs(result); }
    } else if (iguales(linea, "cat")) {
        if (!args[0]) imprimir("Uso: cat RUTA\n");
        else { result = mios_fs_cat(cwd, args, salida, sizeof(salida)); if (result == MIOS_FS_OK) { imprimir(salida); imprimir("\n"); } else error_fs(result); }
    } else if (iguales(linea, "write")) {
        char *texto = args;
        while (*texto && *texto != ' ') texto++;
        if (!args[0] || !*texto) imprimir("Uso: write RUTA TEXTO\n");
        else {
            *texto++ = '\0'; while (*texto == ' ') texto++;
            result = mios_fs_write(cwd, args, texto);
            if (result == MIOS_FS_OK) imprimir("Archivo guardado en disco.\n"); else error_fs(result);
        }
    } else if (iguales(linea, "tree")) {
        result = mios_fs_tree(cwd, args[0] ? args : ".", salida, sizeof(salida));
        if (result == MIOS_FS_OK) {
            if (salida[0]) imprimir(salida);
            else imprimir("(directorio vacio)\n");
        } else error_fs(result);
    } else if (iguales(linea, "rm")) {
        if (!args[0]) imprimir("Uso: rm RUTA\n");
        else {
            result = mios_fs_rm(cwd, args);
            if (result == MIOS_FS_OK) imprimir("Archivo eliminado.\n");
            else error_fs(result);
        }
    } else if (iguales(linea, "rmdir")) {
        if (!args[0]) imprimir("Uso: rmdir RUTA\n");
        else {
            result = mios_fs_rmdir(cwd, args);
            if (result == MIOS_FS_OK) imprimir("Directorio eliminado.\n");
            else error_fs(result);
        }
    } else if (iguales(linea, "reboot")) reiniciar();

    else if (iguales(linea, "ticks")) {
    imprimir("Ticks del sistema: ");
    imprimir_numero(timer_get_ticks());
    imprimir("\n");
      }
    
else if (iguales(linea, "meminfo")) {
    imprimir("=== MEMORIA DE MIOS ===\n");

    imprimir("Paginas totales: ");
    imprimir_numero(memory_total_pages());
    imprimir("\n");

    imprimir("Paginas ocupadas: ");
    imprimir_numero(memory_used_pages());
    imprimir("\n");

    imprimir("Paginas libres: ");
    imprimir_numero(memory_free_pages());
    imprimir("\n");

    imprimir("Tamano por pagina: 4096 bytes\n");
}
else if (iguales(linea, "memtest")) {
    imprimir("Probando administrador de memoria...\n");

    void *pagina1 = memory_alloc_page();
    void *pagina2 = memory_alloc_page();

    if (pagina1 == NULL || pagina2 == NULL) {
        imprimir("ERROR: no hay memoria suficiente.\n");

        if (pagina1 != NULL)
            memory_free_page(pagina1);

        if (pagina2 != NULL)
            memory_free_page(pagina2);
    } else {
        imprimir("Dos paginas reservadas correctamente.\n");

        imprimir("Paginas ocupadas: ");
        imprimir_numero(memory_used_pages());
        imprimir("\n");

        /* Escribir y comprobar datos reales */
        uint8_t *datos = (uint8_t *)pagina1;
        datos[0] = 42;
        datos[4095] = 99;

        if (datos[0] == 42 && datos[4095] == 99)
            imprimir("Lectura y escritura: OK\n");
        else
            imprimir("ERROR: datos incorrectos.\n");

        memory_free_page(pagina1);
        memory_free_page(pagina2);

        imprimir("Paginas liberadas.\n");
    }
}
    else if (iguales(linea, "testexception")) {
    imprimir("Probando excepcion de CPU...\n");
    __asm__ volatile ("int3");
}

else if (iguales(linea, "threadtest")) {
    imprimir("Creando dos hilos...\n\n");


int hilo1 = thread_create(tarea_hilo_1);
int hilo2 = thread_create(tarea_hilo_2);

if (hilo1 < 0 || hilo2 < 0) {
    imprimir("ERROR: no se pudieron crear hilos.\n");

    if (hilo1 >= 0) {
        if (thread_destroy(hilo1) != 0) {
            imprimir("ERROR: limpieza del hilo 1.\n");
        }
    }

    if (hilo2 >= 0) {
        if (thread_destroy(hilo2) != 0) {
            imprimir("ERROR: limpieza del hilo 2.\n");
        }
    }
} else {
    imprimir("Hilos creados: ");
    imprimir_numero(thread_count());
    imprimir("\n");

    if (thread_run() == 0) {
        imprimir("Ambos hilos finalizaron.\n");
    } else {
        imprimir("ERROR al ejecutar los hilos.\n");
    }
}

}

else if (iguales(linea, "schedtest")) {
    imprimir("Prueba Round Robin automatico...\n");

    prueba_hilo2_ejecutado = 0;
    prueba_hilo1_detecto_hilo2 = 0;

    int a = thread_create(sched_tarea1);
    int b = thread_create(sched_tarea2);

    if (a < 0 || b < 0) {
        imprimir("Error al crear hilos.\n");
    } else {
        thread_run();

        if (prueba_hilo1_detecto_hilo2) {
            imprimir("Round Robin: FUNCIONANDO\n");
            imprimir("Hilo 2 ejecuto sin yield voluntario.\n");
        } else {
            imprimir("Round Robin: PRUEBA FALLIDA\n");
        }
    }
}

else if (iguales(linea, "ps")) {
    imprimir("PID   NOMBRE   ESTADO\n");

    for (uint32_t i = 0;
         i < MIOS_MAX_PROCESSES;
         i++) {

        ProcessInfo p;

        if (process_get_info(i, &p) != 0)
            continue;

        if (p.state == PROCESS_FREE)
            continue;

        imprimir_numero(p.pid);
        imprimir("     ");
        imprimir(p.name);
        imprimir("  ASID: ");
imprimir_numero(p.asid);
        imprimir("   ");

        switch (p.state) {
            case PROCESS_READY:
                imprimir("LISTO");
                break;
            case PROCESS_RUNNING:
                imprimir("EJECUTANDO");
                break;
            case PROCESS_TERMINATED:
                imprimir("TERMINADO");
                break;
            default:
                imprimir("DESCONOCIDO");
                break;
        }

        imprimir("\n");
    }
}

else if (iguales(linea, "proctest")) {
    process_init();

    proceso_b_ejecuto = 0;
    proceso_a_observo_b = 0;

    imprimir("Creando procesos...\n");

    int a = process_create(
        "proceso_a",
        tarea_proceso_a
    );

    int b = process_create(
        "proceso_b",
        tarea_proceso_b
    );

    if (a < 0 || b < 0) {
        imprimir("Error al crear procesos.\n");
    } else {
        imprimir("PID del proceso A: ");
        imprimir_numero((uint32_t)a);
        imprimir("\n");

        imprimir("PID del proceso B: ");
        imprimir_numero((uint32_t)b);
        imprimir("\n");

        imprimir("Ejecutando tareas...\n");

        int resultado = process_run();

        if (resultado == 0 &&
            proceso_a_observo_b) {
            imprimir("Procesos ejecutados.\n");
            imprimir("Round Robin: OK\n");
        } else {
            imprimir("Error en la prueba.\n");
        }
    }
}

else if (iguales(linea, "uptime")) {
    uint32_t ticks = timer_get_ticks();

    uint32_t segundos = ticks / 100U;
    uint32_t minutos = segundos / 60U;
    uint32_t horas = minutos / 60U;

    imprimir("=== TIEMPO DE MIOS ===\n");

    imprimir("Horas: ");
    imprimir_numero(horas);
    imprimir("\n");

    imprimir("Minutos: ");
    imprimir_numero(minutos % 60U);
    imprimir("\n");

    imprimir("Segundos: ");
    imprimir_numero(segundos % 60U);
    imprimir("\n");

    imprimir("Ticks totales: ");
    imprimir_numero(ticks);
    imprimir("\n");
}

else if (iguales(linea, "memmap")) {
    imprimir("=== MAPA DE MEMORIA FISICA ===\n");

    if (!memory_map_available()) {
        imprimir("Mapa Multiboot no disponible.\n");
    } else {

        imprimir("Regiones detectadas: ");
        imprimir_numero(memory_map_regions());
        imprimir("\n");

        imprimir("RAM utilizable reportada: ");
        imprimir_numero(memory_map_usable_mib());
        
imprimir("\n=== REGIONES ===\n");

for (uint32_t i = 0;
     i < memory_map_regions();
     i++) {

    MemoryRegionInfo region;

    if (memory_map_get_region(i, &region) != 0) {
        imprimir("Error al leer region.\n");
        break;
    }

    imprimir("Region ");
    imprimir_numero(i);
    imprimir("\n");

    imprimir("  Inicio: ");
    imprimir_hex32((uint32_t)(region.base >> 32));
    imprimir_hex32((uint32_t)region.base);
    imprimir("\n");

    imprimir("  Tamano: ");
    imprimir_hex32((uint32_t)(region.length >> 32));
    imprimir_hex32((uint32_t)region.length);
    imprimir("\n");

    imprimir("  Tipo: ");

    if (region.type == 1)
        imprimir("Utilizable\n");
    else
        imprimir("Reservada u otro tipo\n");
}

        imprimir(" MiB\n");

        imprimir("Asignador fisico: pendiente\n");
    }
}
else if (iguales(linea, "memdebug")) {
    imprimir("=== DIAGNOSTICO DE MEMORIA ===\n");

    imprimir("Kernel inicio: ");
    imprimir_hex32((uint32_t)(uintptr_t)__kernel_start);
    imprimir("\n");

    imprimir("Kernel fin: ");
    imprimir_hex32((uint32_t)(uintptr_t)__kernel_end);
    imprimir("\n");

    imprimir("Mapa Multiboot inicio: ");
    imprimir_hex32(mios_mmap_start);
    imprimir("\n");

    imprimir("Mapa Multiboot longitud: ");
    imprimir_numero(mios_mmap_length);
    imprimir(" bytes\n");
}

else if (iguales(linea, "bootinfo")) {
    imprimir("=== INFORMACION MULTIBOOT ===\n");

    if (mbi_saved_address == 0 ||
        !memory_map_available()) {
        imprimir("Informacion no disponible.\n");
    } else {
        const uint32_t *mbi =
            (const uint32_t *)(uintptr_t)mbi_saved_address;

        uint32_t flags = mbi[0];

        imprimir("Direccion Multiboot: ");
        imprimir_hex32(mbi_saved_address);
        imprimir("\n");

        imprimir("Flags: ");
        imprimir_hex32(flags);
        imprimir("\n");

        imprimir("Modulos cargados: ");

        if (flags & (1U << 3)) {
            imprimir_numero(mbi[5]);

            imprimir("\nDireccion de modulos: ");
            imprimir_hex32(mbi[6]);
        } else {
            imprimir("0 (sin lista de modulos)");
        }

        imprimir("\n");
    }
}

else if (iguales(linea, "pmmtest")) {
    imprimir("=== PRUEBA DE MEMORIA FISICA ===\n");

    uint32_t antes = pmm_free_frames();
    uint32_t a = 0;
    uint32_t b = 0;

    int resultado_a = pmm_alloc_frame(&a);
    int resultado_b = pmm_alloc_frame(&b);

    if (resultado_a != 0 || resultado_b != 0) {
        imprimir("ERROR: no se pudieron reservar marcos.\n");

        if (resultado_a == 0)
            pmm_free_frame(a);

        if (resultado_b == 0)
            pmm_free_frame(b);
    } else {
        imprimir("Marco 1: ");
        imprimir_hex32(a);
        imprimir("\n");

        imprimir("Marco 2: ");
        imprimir_hex32(b);
        imprimir("\n");

        if (a != b &&
            a >= 0x00400000U &&
            b >= 0x00400000U &&
            pmm_free_frames() == antes - 2U) {

            imprimir("Reserva de marcos: OK\n");
        } else {
            imprimir("ERROR: reserva incorrecta.\n");
        }

        int libre_a = pmm_free_frame(a);
        int libre_b = pmm_free_frame(b);

        if (libre_a == 0 &&
            libre_b == 0 &&
            pmm_free_frames() == antes) {

            imprimir("Liberacion: OK\n");
        } else {
            imprimir("ERROR: liberacion incorrecta.\n");
        }

        /* Una segunda liberacion debe fallar */
        if (pmm_free_frame(a) != 0)
            imprimir("Proteccion doble liberacion: OK\n");
        else
            imprimir("ERROR: doble liberacion permitida.\n");
    }
}

else if (iguales(linea, "pmminfo")) {
    imprimir("=== ADMINISTRADOR FISICO MIOS ===\n");

    imprimir("Marcos administrables: ");
    imprimir_numero(pmm_total_frames());
    imprimir("\n");

    imprimir("Marcos libres: ");
    imprimir_numero(pmm_free_frames());
    imprimir("\n");

    imprimir("Marcos asignados: ");
    imprimir_numero(pmm_used_frames());
    imprimir("\n");

    imprimir("Tamano de marco: 4096 bytes\n");
}

else if (iguales(linea, "pmmstress")) {
    uint32_t marcos[16];
    uint32_t cantidad = 0;
    uint32_t libres_antes = pmm_free_frames();
    int error = 0;

    imprimir("=== PRUEBA PMM: 16 MARCOS ===\n");

    for (uint32_t i = 0; i < 16; i++) {
        if (pmm_alloc_frame(&marcos[i]) != 0) {
            error = 1;
            break;
        }

        for (uint32_t j = 0; j < cantidad; j++) {
            if (marcos[j] == marcos[i])
                error = 1;
        }

        cantidad++;
    }

    imprimir("Marcos reservados: ");
    imprimir_numero(cantidad);
    imprimir("\n");

    imprimir("Marcos libres durante la prueba: ");
    imprimir_numero(pmm_free_frames());
    imprimir("\n");

    for (uint32_t i = 0; i < cantidad; i++) {
        if (pmm_free_frame(marcos[i]) != 0)
            error = 1;
    }

    if (pmm_free_frames() != libres_antes)
        error = 1;

    if (error == 0 && cantidad == 16)
        imprimir("PMM STRESS: OK\n");
    else
        imprimir("PMM STRESS: ERROR\n");
}

else if (iguales(linea, "pmmguard")) {
    uint32_t inicio =
        (uint32_t)(uintptr_t)__kernel_start;

    uint32_t fin =
        (uint32_t)(uintptr_t)__kernel_end;

    imprimir("=== PROTECCION DEL KERNEL ===\n");

    imprimir("Inicio: ");
    imprimir_hex32(inicio);
    imprimir("\n");

    imprimir("Fin: ");
    imprimir_hex32(fin);
    imprimir("\n");

    if (pmm_is_reserved(inicio) == 1 &&
        fin > inicio &&
        pmm_is_reserved(fin - 1U) == 1) {
        imprimir("Kernel protegido: OK\n");
    } else {
        imprimir("ERROR: kernel no protegido.\n");
    }

    if (pmm_is_reserved(0x00009000U) == 1)
        imprimir("Memoria baja protegida: OK\n");
    else
        imprimir("ERROR: memoria baja sin proteccion.\n");
}

else if (iguales(linea, "pmmvalidate")) {
    imprimir("=== VALIDACION DEL PMM ===\n");

    if (pmm_validate() == 0) {
        imprimir("Bitmap: OK\n");
        imprimir("Regiones reservadas: OK\n");
        imprimir("Kernel protegido: OK\n");
        imprimir("Contadores de memoria: OK\n");
        imprimir("PMM VALIDATION: OK\n");
    } else {
        color = 0x0C;
        imprimir("PMM VALIDATION: ERROR\n");
        color = 0x0F;
    }
}

else if (iguales(linea, "bootrefs")) {
    imprimir("=== REFERENCIAS MULTIBOOT ===\n");

    if (mbi_saved_address == 0 ||
        !memory_map_available()) {
        imprimir("Multiboot no disponible.\n");
    } else {
        const uint32_t *mbi =
            (const uint32_t *)(uintptr_t)mbi_saved_address;

        uint32_t flags = mbi[0];

        imprimir("MBI principal: ");
        imprimir_hex32(mbi_saved_address);
        imprimir("\n");

        imprimir("Mapa de memoria: ");
        imprimir_hex32(mios_mmap_start);
        imprimir("\n");

        if (flags & (1U << 2)) {
            imprimir("Linea de comandos: ");
            imprimir_hex32(mbi[4]);
            imprimir("\n");
        }

        if (flags & (1U << 5)) {
            imprimir("Tabla ELF: ");
            imprimir_hex32(mbi[9]);
            imprimir("\n");

            imprimir("Entradas ELF: ");
            imprimir_numero(mbi[7]);
            imprimir("\n");

            imprimir("Tamano por entrada: ");
            imprimir_numero(mbi[8]);
            imprimir("\n");
        }

        if (flags & (1U << 9)) {
            imprimir("Nombre de GRUB: ");
            imprimir_hex32(mbi[16]);
            imprimir("\n");
        }

        if (flags & (1U << 11)) {
            imprimir("VBE control: ");
            imprimir_hex32(mbi[18]);
            imprimir("\n");

            imprimir("VBE modo: ");
            imprimir_hex32(mbi[19]);
            imprimir("\n");
        }
    }
}

else if (iguales(linea, "bootvalidate")) {
    imprimir("=== VALIDACION MULTIBOOT ===\n");

    if (memory_map_validate_boot() == 0) {
        imprimir("Estructura principal: OK\n");
        imprimir("Rangos de arranque conocidos: OK\n");
        imprimir("Limite protegido 4 MiB: OK\n");
        imprimir("BOOT VALIDATION: OK\n");
    } else {
        color = 0x0C;
        imprimir("BOOT VALIDATION: ERROR\n");
        color = 0x0F;
    }
}


else if (iguales(linea, "paginginfo")) {
    imprimir("=== PAGINACION DE MIOS ===\n");

    if (!paging_ready()) {
        imprimir("Tablas no preparadas.\n");
    } else {
        imprimir("Tablas de paginas: 32\n");

        imprimir("Espacio mapeado: ");
        imprimir_numero(paging_mapped_mib());
        imprimir(" MiB\n");

        imprimir("Directorio en: ");
        imprimir_hex32(paging_directory_address());
        imprimir("\n");

        imprimir("Kernel: ");
        imprimir(
            paging_is_mapped(0x00100000U)
            ? "MAPEADO\n" : "NO MAPEADO\n"
        );

        imprimir("VGA: ");
        imprimir(
            paging_is_mapped(0x000B8000U)
            ? "MAPEADO\n" : "NO MAPEADO\n"
        );

        imprimir("Paginacion CPU: NO ACTIVADA\n");
    }
}

else if (iguales(linea, "pagingon")) {
    imprimir("=== ACTIVANDO PAGINACION ===\n");

    if (paging_cpu_enabled()) {
        imprimir("Paginacion ya esta activa.\n");
    }
    else if (paging_enable() == 0) {
        imprimir("Paginacion activada: OK\n");
        imprimir("CR0.PG = 1\n");

        imprimir("Directorio CR3: ");
        imprimir_hex32(paging_directory_address());
        imprimir("\n");
    }
    else {
        imprimir("ERROR: no se pudo activar.\n");
    }
}
else if (iguales(linea, "pagingstatus")) {
    imprimir("=== ESTADO DE PAGINACION ===\n");

    if (paging_cpu_enabled())
        imprimir("Paginacion CPU: ACTIVA\n");
    else
        imprimir("Paginacion CPU: INACTIVA\n");
}

else if (iguales(linea, "pagefaulttest")) {
    if (!paging_cpu_enabled()) {
        imprimir("ERROR: paginacion desactivada.\n");
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        imprimir("Provocando Page Fault de prueba...\n");

        volatile uint32_t *direccion =
            (volatile uint32_t *)0x20000000U;

        volatile uint32_t valor = *direccion;
        (void)valor;
    }
}


else if (iguales(linea, "vmmtest")) {
    imprimir("=== PRUEBA DE MEMORIA VIRTUAL ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon primero.\n");
    } else {
        uint32_t marco = 0;
        uint32_t libres_antes = pmm_free_frames();

        if (pmm_alloc_frame(&marco) != 0) {
            imprimir("ERROR: no hay marcos libres.\n");
        } else {
            imprimir("Marco fisico: ");
            imprimir_hex32(marco);
            imprimir("\n");

            if (paging_test_map(marco) != 0) {
                imprimir("ERROR: no se pudo mapear.\n");
                pmm_free_frame(marco);
            } else {
                imprimir("Direccion virtual: ");
                imprimir_hex32(MIOS_TEST_VADDR);
                imprimir("\n");

                volatile uint32_t *virtual_ptr =
                    (volatile uint32_t *)
                    (uintptr_t)MIOS_TEST_VADDR;

                /* Escritura real mediante MMU */
                virtual_ptr[0] = 123456U;
                virtual_ptr[1] = 789012U;

                if (virtual_ptr[0] == 123456U &&
                    virtual_ptr[1] == 789012U) {
                    imprimir("Lectura y escritura: OK\n");
                } else {
                    imprimir("ERROR: datos incorrectos.\n");
                }

                /* Retirar mapeo antes de liberar */
                if (paging_test_unmap() == 0)
                    imprimir("Desmapeo: OK\n");
                else
                    imprimir("ERROR: desmapeo.\n");

                if (pmm_free_frame(marco) == 0)
                    imprimir("Marco liberado: OK\n");
                else
                    imprimir("ERROR: liberacion.\n");
            }

            if (pmm_free_frames() == libres_antes)
                imprimir("Contabilidad PMM: OK\n");
            else
                imprimir("ERROR: diferencia de marcos.\n");
        }
    }
}

else if (iguales(linea, "vmmmulti")) {
    imprimir("=== PRUEBA VMM MULTIPAGINA ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon primero.\n");
    } else {
        uint32_t frames[MIOS_VMM_PAGES] = {0};
        uint32_t allocated = 0;
        uint32_t mapped = 0;
        uint32_t before = pmm_free_frames();
        int success = 1;

        /* Reservar cuatro marcos fisicos */
        for (uint32_t i = 0; i < MIOS_VMM_PAGES; i++) {
            if (pmm_alloc_frame(&frames[i]) != 0) {
                success = 0;
                break;
            }
            allocated++;
        }

        /* Mapear los marcos reservados */
        if (success) {
            for (uint32_t i = 0; i < MIOS_VMM_PAGES; i++) {
                if (vmm_map_page(i, frames[i]) != 0) {
                    success = 0;
                    break;
                }
                mapped++;
            }
        }

        /* Probar escritura y lectura reales */
        if (success) {
            for (uint32_t i = 0; i < MIOS_VMM_PAGES; i++) {
                volatile uint32_t *ptr =
                    (volatile uint32_t *)(uintptr_t)
                    (MIOS_VMM_BASE + i * PAGE_SIZE);

                ptr[0] = 1000U + i;
                ptr[1023] = 2000U + i;
            }

            for (uint32_t i = 0; i < MIOS_VMM_PAGES; i++) {
                volatile uint32_t *ptr =
                    (volatile uint32_t *)(uintptr_t)
                    (MIOS_VMM_BASE + i * PAGE_SIZE);

                if (ptr[0] != 1000U + i ||
                    ptr[1023] != 2000U + i)
                    success = 0;
            }

            if (success)
                imprimir("Lectura y escritura: OK\n");
        }

        /* Retirar los mapeos */
        for (uint32_t i = 0; i < mapped; i++) {
            if (vmm_unmap_page(i) != 0)
                success = 0;
        }

        /* Liberar los marcos del PMM */
        for (uint32_t i = 0; i < allocated; i++) {
            if (pmm_free_frame(frames[i]) != 0)
                success = 0;
        }

        if (pmm_free_frames() != before)
            success = 0;

        if (success) {
            imprimir("Cuatro paginas virtuales: OK\n");
            imprimir("Desmapeo y liberacion: OK\n");
            imprimir("VMM MULTI: OK\n");
        } else {
            imprimir("VMM MULTI: ERROR\n");
        }
    }
}

else if (iguales(linea, "vmmguard")) {
    imprimir("=== PROTECCION DEL VMM ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        uint32_t marco = 0;
        uint32_t libres_antes = pmm_free_frames();
        int ok = 1;

        /* No permitir un marco no asignado */
        if (vmm_map_page(0, 0x00400000U) == 0) {
            imprimir("ERROR: marco no reservado.\n");
            vmm_unmap_page(0);
            ok = 0;
        } else {
            imprimir("Marco no reservado: RECHAZADO\n");
        }

        if (pmm_alloc_frame(&marco) != 0) {
            imprimir("ERROR: reserva PMM.\n");
            ok = 0;
        } else {
            if (vmm_map_page(0, marco) != 0) {
                imprimir("ERROR: primer mapeo.\n");
                ok = 0;
            } else {
                imprimir("Primer mapeo: OK\n");

                if (vmm_map_page(1, marco) == 0) {
                    imprimir("ERROR: duplicacion permitida.\n");
                    vmm_unmap_page(1);
                    ok = 0;
                } else {
                    imprimir("Duplicacion: RECHAZADA\n");
                }

                if (vmm_unmap_page(0) != 0)
                    ok = 0;
            }

            if (pmm_free_frame(marco) != 0)
                ok = 0;
        }

        if (pmm_free_frames() != libres_antes)
            ok = 0;

        if (ok)
            imprimir("VMM GUARD: OK\n");
        else
            imprimir("VMM GUARD: ERROR\n");
    }
}

else if (iguales(linea, "vmmreadonly")) {
    imprimir("=== PRUEBA DE PERMISOS VMM ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: primero ejecuta pagingon.\n");
    } else if (paging_enable_write_protect() != 0) {
        imprimir("ERROR: no se pudo activar CR0.WP.\n");
    } else {
        uint32_t marco = 0;
        uint32_t libres_antes = pmm_free_frames();
        int ok = 1;

        if (vmm_page_mapped(0)) {
            imprimir("ERROR: pagina 0 ya mapeada.\n");
        } else if (pmm_alloc_frame(&marco) != 0) {
            imprimir("ERROR: reserva PMM.\n");
        } else {
            if (vmm_map_page(0, marco) != 0) {
                imprimir("ERROR: mapeo VMM.\n");
                ok = 0;
            } else {
                volatile uint32_t *ptr =
                    (volatile uint32_t *)(uintptr_t)
                    MIOS_VMM_BASE;

                /* Inicialmente es escritura permitida */
                ptr[0] = 12345U;

                if (ptr[0] != 12345U)
                    ok = 0;

                if (vmm_set_writable(0, 0) != 0) {
                    ok = 0;
                } else {
                    imprimir("Pagina configurada: SOLO LECTURA\n");

                    if (ptr[0] == 12345U)
                        imprimir("Lectura protegida: OK\n");
                    else
                        ok = 0;
                }

                if (vmm_unmap_page(0) != 0)
                    ok = 0;
            }

            if (pmm_free_frame(marco) != 0)
                ok = 0;
        }

        if (pmm_free_frames() != libres_antes)
            ok = 0;

        if (ok && paging_write_protect_enabled())
            imprimir("VMM READONLY: OK\n");
        else
            imprimir("VMM READONLY: ERROR\n");
    }
}

else if (iguales(linea, "vmmwritetest")) {
    imprimir("=== PRUEBA DE PROTECCION VMM ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon primero.\n");
    }
    else if (paging_enable_write_protect() != 0) {
        imprimir("ERROR: no se pudo activar CR0.WP.\n");
    }
    else if (vmm_page_mapped(0)) {
        imprimir("ERROR: pagina virtual ocupada.\n");
    }
    else {
        uint32_t marco = 0;

        if (pmm_alloc_frame(&marco) != 0) {
            imprimir("ERROR: no hay marcos libres.\n");
        }
        else if (vmm_map_page(0, marco) != 0) {
            imprimir("ERROR: no se pudo mapear.\n");
            pmm_free_frame(marco);
        }
        else {
            volatile uint32_t *ptr =
                (volatile uint32_t *)(uintptr_t)
                MIOS_VMM_BASE;

            /* Primero escribir con permiso */
            ptr[0] = 12345U;

            if (ptr[0] != 12345U) {
                imprimir("ERROR: lectura inicial.\n");

                if (vmm_unmap_page(0) == 0)
                    pmm_free_frame(marco);
            }
            else if (vmm_set_writable(0, 0) != 0) {
                imprimir("ERROR: cambiar permisos.\n");

                if (vmm_unmap_page(0) == 0)
                    pmm_free_frame(marco);
            }
            else {
                imprimir("Pagina: SOLO LECTURA\n");
                imprimir("CR0.WP: ACTIVADO\n");
                imprimir("Intentando escritura prohibida...\n");

                /*
                 * Esta escritura debe provocar
                 * Page Fault, vector 14.
                 */
                ptr[0] = 99999U;

                /*
                 * Si llega aqui, la proteccion
                 * no funciono como esperabamos.
                 */
                imprimir("ERROR: escritura permitida.\n");

                if (vmm_unmap_page(0) == 0)
                    pmm_free_frame(marco);
            }
        }
    }
}

else if (iguales(linea, "pmmownership")) {
    imprimir("=== PROTECCION DE MARCOS ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else if (vmm_page_mapped(0)) {
        imprimir("ERROR: pagina de prueba ocupada.\n");
    } else {
        uint32_t marco = 0;
        uint32_t libres_antes = pmm_free_frames();
        int ok = 1;

        if (pmm_alloc_frame(&marco) != 0) {
            imprimir("ERROR: reserva fisica.\n");
        } else if (vmm_map_page(0, marco) != 0) {
            imprimir("ERROR: mapeo virtual.\n");
            pmm_free_frame(marco);
        } else {
            if (pmm_frame_pin_count(marco) == 1)
                imprimir("Referencia de mapeo: OK\n");
            else
                ok = 0;

            /* Debe rechazar esta liberacion */
            if (pmm_free_frame(marco) != 0)
                imprimir("Liberacion prematura: BLOQUEADA\n");
            else {
                imprimir("ERROR: liberacion permitida.\n");
                ok = 0;
            }

            if (vmm_unmap_page(0) == 0)
                imprimir("Desmapeo: OK\n");
            else
                ok = 0;

            if (pmm_frame_pin_count(marco) != 0)
                ok = 0;

            if (pmm_free_frame(marco) == 0)
                imprimir("Liberacion posterior: OK\n");
            else
                ok = 0;

            if (pmm_free_frames() != libres_antes)
                ok = 0;

            if (ok)
                imprimir("PMM OWNERSHIP: OK\n");
            else
                imprimir("PMM OWNERSHIP: ERROR\n");
        }
    }
}

else if (iguales(linea, "irqtest")) {
    imprimir("=== PRUEBA SECCION CRITICA ===\n");

    int estado_antes = irq_enabled();

    uint32_t ticks_antes = timer_get_ticks();

    uint32_t flags = irq_save();

    int estado_dentro = irq_enabled();

    /*
     * Segunda entrada a una seccion critica.
     * Comprueba que la restauracion anidada
     * no habilite interrupciones antes de tiempo.
     */
    uint32_t flags_anidados = irq_save();

    irq_restore(flags_anidados);

    int sigue_deshabilitado = !irq_enabled();

    irq_restore(flags);

    int estado_despues = irq_enabled();

    uint32_t ticks_despues = timer_get_ticks();

    if (estado_antes &&
        !estado_dentro &&
        sigue_deshabilitado &&
        estado_despues) {

        imprimir("Guardar EFLAGS: OK\n");
        imprimir("Deshabilitar IRQ: OK\n");
        imprimir("Restauracion anidada: OK\n");
        imprimir("Restaurar interrupciones: OK\n");
        imprimir("IRQ TEST: OK\n");
    } else {
        imprimir("IRQ TEST: ERROR\n");
    }

    imprimir("Ticks antes: ");
    imprimir_numero(ticks_antes);
    imprimir("\n");

    imprimir("Ticks despues: ");
    imprimir_numero(ticks_despues);
    imprimir("\n");
}

else if (iguales(linea, "pmmconcurrent")) {
    imprimir("=== PRUEBA CONCURRENCIA PMM ===\n");

    concurrent_live[0] = 0;
    concurrent_live[1] = 0;
    concurrent_done[0] = 0;
    concurrent_done[1] = 0;
    concurrent_errors = 0;
    concurrent_overlap = 0;

    uint32_t before = pmm_free_frames();

    int a = thread_create(pmm_concurrent_task1);
    int b = thread_create(pmm_concurrent_task2);

    if (a < 0 || b < 0) {
        imprimir("ERROR: no se pudieron crear hilos.\n");
        /*
         * No continuar con thread_run si
         * falta uno de los dos hilos.
         * La limpieza de creaciones parciales
         * sera una mejora del administrador.
         */
    } else {
        int run_result = thread_run();

        imprimir("Operaciones hilo 1: ");
        imprimir_numero(concurrent_done[0]);
        imprimir("\n");

        imprimir("Operaciones hilo 2: ");
        imprimir_numero(concurrent_done[1]);
        imprimir("\n");

        imprimir("Coincidencias temporales: ");
        imprimir_numero(concurrent_overlap);
        imprimir("\n");

        imprimir("Errores: ");
        imprimir_numero(concurrent_errors);
        imprimir("\n");

        if (run_result == 0 &&
            concurrent_done[0] == PMM_CONCURRENT_LOOPS &&
            concurrent_done[1] == PMM_CONCURRENT_LOOPS &&
            concurrent_errors == 0 &&
            concurrent_overlap > 0 &&
            concurrent_live[0] == 0 &&
            concurrent_live[1] == 0 &&
            pmm_free_frames() == before) {

            imprimir("PMM CONCURRENT: OK\n");
        } else {
            imprimir("PMM CONCURRENT: ERROR\n");
        }
    }
}

else if (iguales(linea, "threadcleanup")) {
    imprimir("=== PRUEBA DE LIMPIEZA DE HILOS ===\n");

    uint32_t libres_antes = memory_free_pages();
    uint32_t hilos_antes = thread_count();

    int tid = thread_create(pmm_concurrent_task1);

    if (tid < 0) {
        imprimir("ERROR: no se pudo crear el hilo.\n");
    } else {
        imprimir("Hilo creado.\n");

        if (thread_destroy(tid) == 0 &&
            memory_free_pages() == libres_antes &&
            thread_count() == hilos_antes) {

            imprimir("Pila liberada: OK\n");
            imprimir("Descriptor recuperado: OK\n");
            imprimir("THREAD CLEANUP: OK\n");
        } else {
            imprimir("THREAD CLEANUP: ERROR\n");
        }
    }
}

else if (iguales(linea, "vmmconcurrent")) {
    imprimir("=== CONCURRENCIA DEL VMM ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    }
    else if (vmm_page_mapped(0) ||
             vmm_page_mapped(1)) {
        imprimir("ERROR: paginas de prueba ocupadas.\n");
    }
    else {
        vmm_done[0] = vmm_done[1] = 0;
        vmm_errors[0] = vmm_errors[1] = 0;
        vmm_active[0] = vmm_active[1] = 0;
        vmm_overlap = 0;

        uint32_t before = pmm_free_frames();

        int a = thread_create(vmm_concurrent_task1);
        int b = thread_create(vmm_concurrent_task2);

        if (a < 0 || b < 0) {
            imprimir("ERROR: creacion de hilos.\n");

            if (a >= 0)
                thread_destroy(a);

            if (b >= 0)
                thread_destroy(b);
        } else {
            int result = thread_run();

            imprimir("Operaciones hilo 1: ");
            imprimir_numero(vmm_done[0]);
            imprimir("\n");

            imprimir("Operaciones hilo 2: ");
            imprimir_numero(vmm_done[1]);
            imprimir("\n");

            imprimir("Errores hilo 1: ");
            imprimir_numero(vmm_errors[0]);
            imprimir("\n");

            imprimir("Errores hilo 2: ");
            imprimir_numero(vmm_errors[1]);
            imprimir("\n");

            int ok =
                result == 0 &&
                vmm_done[0] == VMM_CONCURRENT_LOOPS &&
                vmm_done[1] == VMM_CONCURRENT_LOOPS &&
                vmm_errors[0] == 0 &&
                vmm_errors[1] == 0 &&
                vmm_overlap != 0 &&
                !vmm_page_mapped(0) &&
                !vmm_page_mapped(1) &&
                pmm_free_frames() == before;

            if (ok) {
                imprimir("Ejecucion superpuesta: OK\n");
                imprimir("Mapeos y liberacion: OK\n");
                imprimir("VMM CONCURRENT: OK\n");
            } else {
                imprimir("VMM CONCURRENT: ERROR\n");
            }
        }
    }
}
else if (iguales(linea, "spacetest")) {
    imprimir("=== ESPACIOS DE DIRECCIONES ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        uint32_t antes = pmm_free_frames();

        if (paging_address_spaces_test() == 0 &&
            pmm_free_frames() == antes) {

            imprimir("Directorio A: OK\n");
            imprimir("Directorio B: OK\n");
            imprimir("Kernel compartido: OK\n");
            imprimir("Marcos privados distintos: OK\n");
            imprimir("Liberacion de memoria: OK\n");
            imprimir("SPACE TEST: OK\n");
        } else {
            imprimir("SPACE TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "cr3test")) {
    imprimir("=== PRUEBA CAMBIO CR3 ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        uint32_t libres_antes = pmm_free_frames();

        int resultado = paging_cr3_switch_test();

        if (resultado == 0 &&
            pmm_free_frames() == libres_antes) {

            imprimir("Directorio A: valor 1111 OK\n");
            imprimir("Directorio B: valor 2222 OK\n");
            imprimir("Misma direccion virtual: OK\n");
            imprimir("Cambio real de CR3: OK\n");
            imprimir("Restauracion de CR3: OK\n");
            imprimir("Liberacion de marcos: OK\n");
            imprimir("CR3 TEST: OK\n");
        } else {
            color = 0x0C;
            imprimir("CR3 TEST: ERROR\n");
            color = 0x0F;
        }
    }
}
else if (iguales(linea, "ascreate")) {
    int id = as_create();

    if (id < 0) {
        imprimir("ERROR: no se pudo crear espacio.\n");
    } else {
        imprimir("Espacio creado: ");
        imprimir_numero((uint32_t)id);
        imprimir("\n");
    }
}
else if (iguales(linea, "aslist")) {
    imprimir("=== ESPACIOS DE DIRECCIONES ===\n");
    imprimir("ID   CR3          MARCO PRIVADO\n");

    for (uint32_t id = 1; id <= MIOS_AS_MAX; id++) {
        uint32_t cr3 = 0;
        uint32_t frame = 0;

        if (as_get(id, &cr3, &frame) != 0)
            continue;

        imprimir_numero(id);
        imprimir("    ");

        imprimir_hex32(cr3);
        imprimir("   ");

        imprimir_hex32(frame);
        imprimir("\n");
    }

    imprimir("Total: ");
    imprimir_numero(as_count());
    imprimir("\n");
}
else if (iguales(linea, "asdestroy")) {
    uint32_t id = 0;

    if (iguales(args, "1"))
        id = 1;
    else if (iguales(args, "2"))
        id = 2;

    if (id == 0) {
        imprimir("Uso: asdestroy 1|2\n");
    } else {
        int result = as_destroy(id);

        if (result == 0) {
            imprimir("Espacio destruido: ");
            imprimir_numero(id);
            imprimir("\n");
        } else if (result == -2) {
            imprimir("ERROR: espacio activo.\n");
        } else {
            imprimir("ERROR: no se pudo destruir.\n");
        }
    }
}

else if (iguales(linea, "asbusytest")) {
    imprimir("=== PRUEBA PROTECCION AS ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int id = as_create();

        if (id < 0) {
            imprimir("ERROR: crear espacio.\n");
        } else {
            int ok = 1;
            int acquired = 0;

            if (as_acquire((uint32_t)id) != 0) {
                imprimir("ERROR: registrar usuario.\n");
                ok = 0;
            } else {
                acquired = 1;

                if (as_users((uint32_t)id) == 1)
                    imprimir("Usuario registrado: OK\n");
                else
                    ok = 0;

                if (as_destroy((uint32_t)id) == -2)
                    imprimir("Destruccion prematura: BLOQUEADA\n");
                else {
                    imprimir("ERROR: proteccion de espacio.\n");
                    ok = 0;
                }
            }

            if (acquired) {
                if (as_release((uint32_t)id) != 0) {
                    imprimir("ERROR: liberar usuario.\n");
                    ok = 0;
                } else {
                    acquired = 0;
                }
            }

            if (!acquired) {
                if (as_destroy((uint32_t)id) == 0)
                    imprimir("Destruccion posterior: OK\n");
                else {
                    imprimir("ERROR: destruir espacio.\n");
                    ok = 0;
                }
            }

            if (pmm_free_frames() != before)
                ok = 0;

            if (ok)
                imprimir("AS BUSY TEST: OK\n");
            else
                imprimir("AS BUSY TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "processcr3test")) {
    imprimir("=== PRUEBA CR3 POR PROCESO ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("Primero ejecuta pagingon.\n");
    } else {
        proc_cr3_result[0] = 0;
        proc_cr3_result[1] = 0;
        proc_cr3_errors[0] = 0;
        proc_cr3_errors[1] = 0;

        uint32_t before = pmm_free_frames();

        int a = process_create(
            "cr3_proceso_a",
            process_cr3_task_a
        );

        int b = process_create(
            "cr3_proceso_b",
            process_cr3_task_b
        );

        if (a < 0 || b < 0) {
            imprimir("ERROR: creacion de procesos.\n");
            imprimir("No ejecutar thread_run.\n");
        } else {
            int result = process_run();

            if (result == 0 &&
                proc_cr3_result[0] == 1111U &&
                proc_cr3_result[1] == 2222U &&
                proc_cr3_errors[0] == 0 &&
                proc_cr3_errors[1] == 0 &&
                pmm_free_frames() == before) {

                imprimir("Proceso A: 1111 OK\n");
                imprimir("Proceso B: 2222 OK\n");
                imprimir("Cambio automatico de CR3: OK\n");
                imprimir("PROCESS CR3 TEST: OK\n");
            } else {
                imprimir("PROCESS CR3 TEST: ERROR\n");
            }
        }
    }
}

else if (iguales(linea, "procclean")) {
    imprimir("=== LIMPIEZA DE PROCESOS ===\n");

    int recovered = process_reap();

    imprimir("Descriptores recuperados: ");
    imprimir_numero((uint32_t)recovered);
    imprimir("\n");

    imprimir("PROCESS CLEANUP: OK\n");
}

/* MiOS - Diagnostico de la GDT actual */



else if (iguales(linea, "gdtinfo")) {
    mostrar_gdt();
}
else if (iguales(linea, "gdtcheck")) {
    imprimir("=== GDT DE MIOS ===\n");

    if (gdt_verify()) {
        imprimir("Codigo kernel: OK\n");
        imprimir("Datos kernel: OK\n");
        imprimir("Pila kernel: OK\n");
        imprimir("GDT CHECK: OK\n");
    } else {
        imprimir("GDT CHECK: ERROR\n");
    }
}
else if (iguales(linea, "tsscheck")) {
    imprimir("=== TSS DE MIOS ===\n");

    if (tss_verify()) {
        imprimir("Descriptor TSS: OK\n");
        imprimir("Selector TR: 0x00000028\n");
        imprimir("SS0 kernel: OK\n");
        imprimir("ESP0 kernel: OK\n");
        imprimir("TSS CHECK: OK\n");
    } else {
        imprimir("TSS CHECK: ERROR\n");
    }
}

else if (iguales(linea, "tssstacktest")) {
    imprimir("=== PRUEBA TSS POR HILO ===\n");

    tss_test_ok[0] = 0;
    tss_test_ok[1] = 0;

    int a = thread_create(tss_test_task1);
    int b = thread_create(tss_test_task2);

    if (a < 0 || b < 0) {
        if (a >= 0)
            thread_destroy(a);

        if (b >= 0)
            thread_destroy(b);

        imprimir("ERROR: creacion de hilos.\n");
    } else {
        int result = thread_run();

        if (result == 0 &&
            tss_test_ok[0] == 1 &&
            tss_test_ok[1] == 1 &&
            tss_verify()) {

            imprimir("Pila del hilo 1: OK\n");
            imprimir("Pila del hilo 2: OK\n");
            imprimir("TSS restaurada: OK\n");
            imprimir("TSS STACK TEST: OK\n");
        } else {
            imprimir("TSS STACK TEST: ERROR\n");
        }
    }
}

    else {
        color = 0x0C; imprimir("Comando desconocido: "); imprimir(linea);
        imprimir("\nEscribe help para ver los comandos.\n"); color = 0x0F;
    }
}

/* Scancode set 1: mapa base tipo US. */
/* Teclado español de España - QWERTY */
static const char mapa_normal[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4',
    [0x06]='5', [0x07]='6', [0x08]='7', [0x09]='8',
    [0x0A]='9', [0x0B]='0',

    [0x0C]='\'',
 [0x0D] = (char)0xAD,  /* ¡ */

    [0x10]='q', [0x11]='w', [0x12]='e',
    [0x13]='r', [0x14]='t', [0x15]='y',
    [0x16]='u', [0x17]='i', [0x18]='o',
    [0x19]='p',

    [0x1A]='`',
    [0x1B]='+',

    [0x1E]='a', [0x1F]='s', [0x20]='d',
    [0x21]='f', [0x22]='g', [0x23]='h',
    [0x24]='j', [0x25]='k', [0x26]='l',
  /* ñ *//* ñ: requiere soporte de codificación */
    [0x28]='\'',
[0x29] = (char)0xA7,  /* º */
[0x2B] = (char)0x87,  /* ç */
[0x27]=0xA4,  /* ñ */
    [0x2C]='z', [0x2D]='x', [0x2E]='c',
    [0x2F]='v', [0x30]='b', [0x31]='n',
    [0x32]='m',

    [0x33]=',',
    [0x34]='.',
    [0x35]='-',

    [0x39]=' ',
    [0x56]='<'
};
static const char mapa_shift[128] = {
    [0x02]='!',
    [0x03]='"',
    [0x04]='\'',
    [0x05]='$',
    [0x06]='%',
    [0x07]='&',
    [0x08]='/',
    [0x09]='(',
    [0x0A]=')',
    [0x0B]='=',

    [0x0C]='?',
   

    [0x1A]='^',
    [0x1B]='*',

    [0x27]=':', /* Ñ: requiere soporte de codificación */
    [0x28]='"',
    [0x0D] = (char)0xA8,  /* ¿ */
[0x29] = (char)0xA6,  /* ª */
[0x2B] = (char)0x80,  /* Ç */
    [0x33]=';',
    [0x34]=':',
    [0x35]='_',
    [0x56]='>'
};
static char traducir_tecla(uint8_t codigo, int shift, int caps) {
    if (codigo >= 128) return 0;
    char c = mapa_normal[codigo];
    if (!c) return 0;

    if (codigo == 0x27) {
    c = (shift ^ caps) ? mapa_shift[codigo] : mapa_normal[codigo];
}
if (c >= 'a' && c <= 'z') {
    if (shift ^ caps)
        c = (char)(c - 'a' + 'A');
}
else if ((uint8_t)c == 0xA4) {
    if (shift ^ caps)
        c = (char)0xA5;
}
else if (shift && mapa_shift[codigo]) {
    c = mapa_shift[codigo];
}
else if (shift && mapa_shift[codigo]) {
    c = mapa_shift[codigo];
}
    return c;
}

void kernel_main(uint32_t multiboot_magic,
    uint32_t multiboot_info) {
        mbi_saved_address = multiboot_info;
            memory_map_init(multiboot_magic, multiboot_info);
            int pmm_status = pmm_init();
(void)pmm_status;
paging_init();
as_init();
    mios_fs_init();
    memory_init();
    thread_init();
    process_init();
    color = 0x0F;
    limpiar_pantalla();
    tss_prepare();
    gdt_init();
    tss_load();
 interrupts_init();
    color = 0x0A;
    imprimir("================================\n");
    imprimir("MIOS - SISTEMA OPERATIVO v0.5\n");
    imprimir("================================\n\n");

    color = 0x0F;
    imprimir("Sistema de archivos: RAM\n");
    imprimir("Escribe help para ver los comandos.\n\n");

    char entrada[BUFFER_SIZE];
    size_t longitud = 0;
    int shift = 0;
    int caps = 0;
    int extendido = 0;
    int ignorar_pausa = 0;

    mostrar_prompt();

    while (1) {
        if (!(inb(0x64) & 1)) continue;
        uint8_t codigo = inb(0x60);

        if (ignorar_pausa) { ignorar_pausa--; continue; }
        if (codigo == 0xE1) { ignorar_pausa = 5; continue; }
        if (codigo == 0xE0) { extendido = 1; continue; }
        if (extendido) { extendido = 0; continue; }

        if (codigo == 0x2A || codigo == 0x36) { shift = 1; continue; }
        if (codigo == 0xAA || codigo == 0xB6) { shift = 0; continue; }
        if (codigo == 0x3A) { caps = !caps; continue; }
        if (codigo & 0x80) continue;

        if (codigo == 0x1C) {
            entrada[longitud] = '\0';
            poner_caracter('\n');
            ejecutar_comando(entrada);
            longitud = 0;
            mostrar_prompt();
        } else if (codigo == 0x0E) {
            if (longitud > 0) {
                longitud--;
                poner_caracter('\b');
            }
        } else {
            char c = traducir_tecla(codigo, shift, caps);
            if (c && longitud < BUFFER_SIZE - 1) {
                entrada[longitud++] = c;
                poner_caracter(c);
            }
        }
    }
}
static void imprimir_numero(uint32_t numero) {
    char buffer[11];
    int i = 0;

    if (numero == 0) {
        poner_caracter('0');
        return;
    }

    while (numero > 0) {
        buffer[i++] = '0' + (numero % 10);
        numero /= 10;
    }

    while (i > 0) {
        poner_caracter(buffer[--i]);
    }
}
static void imprimir_numero(uint32_t numero);
