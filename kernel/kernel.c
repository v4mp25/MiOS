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
#include "syscall.h"
#include "user_program.h"
#include "ring3.h"
#include "user_context.h"
#include "user_tasks.h"
#include "user_scheduler.h"
#include "user_process.h"
#include "elf32.h"
#include "elf_disk.h"
#include "binfs.h"
#include "elf_exec.h"
#include "binfs.h"
#include "binfs_verify.h"
#include "binfs_mirror.h"
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

/*
 * Pila privilegiada para el primer
 * programa de usuario.
 *
 * No usar la pila de usuario como ESP0.
 */
static uint8_t ring3_test_kernel_stack[16384]
    __attribute__((aligned(16)));

/*
 * Construye una imagen ELF de prueba.
 * No contiene codigo ejecutable real.
 */
static int elf32_selftest(void) {
    uint8_t image[
        ELF32_EHDR_SIZE + ELF32_PHDR_SIZE
    ];

    for (uint32_t i = 0;
         i < sizeof(image);
         i++) {
        image[i] = 0;
    }

    Elf32_Ehdr header = {0};

    header.e_ident[0] = ELF32_MAGIC0;
    header.e_ident[1] = ELF32_MAGIC1;
    header.e_ident[2] = ELF32_MAGIC2;
    header.e_ident[3] = ELF32_MAGIC3;

    header.e_ident[4] = ELFCLASS32;
    header.e_ident[5] = ELFDATA2LSB;
    header.e_ident[6] = EV_CURRENT;

    header.e_type = ET_EXEC;
    header.e_machine = EM_386;
    header.e_version = EV_CURRENT;

    header.e_entry = 0x10800000U;

    header.e_ehsize = sizeof(Elf32_Ehdr);
    header.e_phoff = sizeof(Elf32_Ehdr);

    header.e_phentsize = sizeof(Elf32_Phdr);
    header.e_phnum = 1;

    /* Copiar cabecera al inicio del archivo */
    const uint8_t *src = (const uint8_t *)&header;

    for (uint32_t i = 0;
         i < sizeof(header);
         i++) {
        image[i] = src[i];
    }

    Elf32_Ehdr parsed;

    /* Caso valido estructuralmente */
    if (elf32_validate(
            image,
            sizeof(image),
            &parsed
        ) != 0) {
        return -1;
    }

    if (parsed.e_entry != 0x10800000U ||
        parsed.e_machine != EM_386 ||
        parsed.e_phnum != 1U) {
        return -1;
    }

    /* Rechazar un archivo demasiado corto */
    if (elf32_validate(
            image,
            ELF32_EHDR_SIZE - 1U,
            &parsed
        ) == 0) {
        return -1;
    }

    /* Rechazar tabla de programas incompleta */
    if (elf32_validate(
            image,
            ELF32_EHDR_SIZE,
            &parsed
        ) == 0) {
        return -1;
    }

    /* Rechazar identificacion ELF incorrecta */
    image[0] = 0;

    if (elf32_validate(
            image,
            sizeof(image),
            &parsed
        ) == 0) {
        return -1;
    }

    image[0] = ELF32_MAGIC0;

    /* Rechazar arquitectura incorrecta */
    image[18] = 0;

    if (elf32_validate(
            image,
            sizeof(image),
            &parsed
        ) == 0) {
        return -1;
    }

    return 0;
}

/*
 * Prueba de segmentos ELF32.
 * No ejecuta codigo de usuario.
 */
static int elf32_segment_selftest(void) {
    uint8_t image[128];

    for (uint32_t i = 0; i < sizeof(image); i++)
        image[i] = 0;

    Elf32_Ehdr header = {0};
    Elf32_Phdr segment = {0};

    header.e_ident[0] = ELF32_MAGIC0;
    header.e_ident[1] = ELF32_MAGIC1;
    header.e_ident[2] = ELF32_MAGIC2;
    header.e_ident[3] = ELF32_MAGIC3;
    header.e_ident[4] = ELFCLASS32;
    header.e_ident[5] = ELFDATA2LSB;
    header.e_ident[6] = EV_CURRENT;

    header.e_type = ET_EXEC;
    header.e_machine = EM_386;
    header.e_version = EV_CURRENT;
    header.e_entry = MIOS_ELF_USER_BASE;
    header.e_ehsize = sizeof(Elf32_Ehdr);
    header.e_phoff = sizeof(Elf32_Ehdr);
    header.e_phentsize = sizeof(Elf32_Phdr);
    header.e_phnum = 1;

    segment.p_type = PT_LOAD;
    segment.p_offset = 84U;
    segment.p_vaddr = MIOS_ELF_USER_BASE;
    segment.p_filesz = 4U;
    segment.p_memsz = 8U;
    segment.p_flags = ELF_PF_R | ELF_PF_X;
    segment.p_align = 1U;

    /*
     * Copiar cabecera y encabezado de
     * programa a la imagen.
     */
    const uint8_t *h =
        (const uint8_t *)&header;

    for (uint32_t i = 0; i < sizeof(header); i++)
        image[i] = h[i];

    const uint8_t *p =
        (const uint8_t *)&segment;

    for (uint32_t i = 0; i < sizeof(segment); i++)
        image[52U + i] = p[i];

    /* Cuatro bytes de instrucciones de prueba */
    image[84] = 0x90;
    image[85] = 0x90;
    image[86] = 0x90;
    image[87] = 0xC3;

    Elf32_LoadInfo info;

    /* Caso valido */
    if (elf32_validate_segments(
            image, sizeof(image), &info
        ) != 0)
        return -1;

    if (info.load_segments != 1U ||
        info.loaded_bytes != 4U ||
        info.memory_bytes != 8U ||
        info.entry != MIOS_ELF_USER_BASE)
        return -1;

    /* Error: archivos mas cortos que el segmento */
    if (elf32_validate_segments(
            image, 86U, &info
        ) == 0)
        return -1;

    /*
     * Error: p_filesz mayor que p_memsz.
     * p_filesz comienza en offset 16
     * del encabezado de programa.
     */
    image[52U + 16U] = 9U;

    if (elf32_validate_segments(
            image, sizeof(image), &info
        ) == 0)
        return -1;

    image[52U + 16U] = 4U;

    /* Error: punto de entrada fuera del codigo */
    image[24] = 0x00;
    image[25] = 0x10;
    image[26] = 0x80;
    image[27] = 0x10;

    if (elf32_validate_segments(
            image, sizeof(image), &info
        ) == 0)
        return -1;

    return 0;
}


static int elf32_load_selftest(void) {
    /*
     * Imagen ELF sintetica:
     * cabecera + encabezado PT_LOAD +
     * instrucciones en offset 128.
     */
    uint8_t image[160] = {0};

    Elf32_Ehdr header = {0};
    Elf32_Phdr segment = {0};

    header.e_ident[0] = ELF32_MAGIC0;
    header.e_ident[1] = ELF32_MAGIC1;
    header.e_ident[2] = ELF32_MAGIC2;
    header.e_ident[3] = ELF32_MAGIC3;
    header.e_ident[4] = ELFCLASS32;
    header.e_ident[5] = ELFDATA2LSB;
    header.e_ident[6] = EV_CURRENT;

    header.e_type = ET_EXEC;
    header.e_machine = EM_386;
    header.e_version = EV_CURRENT;
    header.e_entry = MIOS_ELF_USER_BASE;
    header.e_ehsize = sizeof(Elf32_Ehdr);
    header.e_phoff = sizeof(Elf32_Ehdr);
    header.e_phentsize = sizeof(Elf32_Phdr);
    header.e_phnum = 1;

    segment.p_type = PT_LOAD;
    segment.p_offset = 128U;
    segment.p_vaddr = MIOS_ELF_USER_BASE;
    segment.p_filesz = 14U;
    segment.p_memsz = 32U;
    segment.p_flags = ELF_PF_R | ELF_PF_X;
    segment.p_align = 1U;

    const uint8_t *h =
        (const uint8_t *)&header;

    for (uint32_t i = 0;
         i < sizeof(header); i++) {
        image[i] = h[i];
    }

    const uint8_t *p =
        (const uint8_t *)&segment;

    for (uint32_t i = 0;
         i < sizeof(segment); i++) {
        image[52U + i] = p[i];
    }

    /*
     * Programa de ejemplo:
     * SYS_TEST, SYS_EXIT, UD2.
     */
    static const uint8_t code[14] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xB8, 0x02, 0x00, 0x00, 0x00,
        0xCD, 0x80
    };

    for (uint32_t i = 0; i < sizeof(code); i++) {
        image[128U + i] = code[i];
    }

    Elf32_LoadInfo info;

    if (elf32_validate_segments(
            image, sizeof(image), &info
        ) != 0) {
        return -1;
    }

    int id = as_create();

    if (id < 0)
        return -1;

    int ok = 1;

    uint32_t cr3 = 0;
    uint32_t physical_frame = 0;
    uint32_t entry = 0;

    if (as_get(
            (uint32_t)id,
            &cr3,
            &physical_frame
        ) != 0) {
        ok = 0;
    }

    if (ok && elf32_load(
            image,
            sizeof(image),
            (uint32_t)id,
            &entry
        ) != 0) {
        ok = 0;
    }

    if (ok) {
        const uint8_t *loaded =
            (const uint8_t *)(uintptr_t)
            physical_frame;

        if (entry != MIOS_ELF_USER_BASE)
            ok = 0;

        /* Los bytes del ELF coinciden */
        for (uint32_t i = 0;
             i < sizeof(code); i++) {

            if (loaded[i] != code[i])
                ok = 0;
        }

        /* BSS y resto de pagina a cero */
        for (uint32_t i = sizeof(code);
             i < 4096U; i++) {

            if (loaded[i] != 0)
                ok = 0;
        }
    }

    if (as_destroy((uint32_t)id) != 0)
        ok = 0;

    return ok ? 0 : -1;
}
/* Bytes del archivo user/hola.elf enlazado */
extern const uint8_t _binary_user_hola_elf_start[];
extern const uint8_t _binary_user_hola_elf_end[];

static uint8_t elf_test_kernel_stack[16384]
    __attribute__((aligned(16)));

    static uint8_t elf_disk_buffer[MIOS_ELF_DISK_MAX];
    /* Pila Ring 0 para ejecutar el ELF desde ATA */
static uint8_t elf_disk_run_kernel_stack[16384]
    __attribute__((aligned(16)));
    static uint8_t binfs_test_buffer[BINFS_MAX_FILE_SIZE];
    
/*
 * Salida del kernel para SYS_WRITE.
 * Recibe un caracter que ya fue
 * validado como dato de usuario.
 */
void syscall_console_putc(char c) {
    char text[2];

    text[0] = c;
    text[1] = '\0';

    imprimir(text);
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
    imprimir("usermaptest\n");
    imprimir("userstacktest\n");
    imprimir("syscalltest\n");
    imprimir("usercodetest\n");
    imprimir("syscall2test\n");
    imprimir("ring3test\n");
    imprimir("ring3verify\n");
    imprimir("usertasktest\n");
    imprimir("userrrtest\n");
    imprimir("usercontextswitchtest\n");
    imprimir("userframecheck\n");
    imprimir("usermultitest\n");
    imprimir("userproctest\n");
    imprimir("userpsdemo\n");
    imprimir("userpidtest\n");
    imprimir("elftest\n");
    imprimir("elfsegmenttest\n");
    imprimir("elfloadtest\n");
    imprimir("elfruntest\n");
    imprimir("elfdisktest\n");
    imprimir("elfdiskrun\n");   
    imprimir("binfstest\n");
    imprimir("run archivo.elf\n");
    imprimir("syswritestats\n");
    imprimir("binls\n");
    imprimir("bincopy origen.elf destino.elf\n");
    imprimir("binrename anterior.elf nuevo.elf\n");
    imprimir("binrm archivo.elf\n");
    imprimir("binverify\n");
    imprimir("binreplace origen.elf destino.elf\n");
    imprimir("binmirrorcheck\n");
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

else if (iguales(linea, "usermaptest")) {
    imprimir("=== PRUEBA DE PAGINAS USER ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0) {
        imprimir("Activa pagingon y libera los espacios previos.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int id = as_create();

        if (id < 0) {
            imprimir("ERROR: crear espacio.\n");
        } else {
            uint32_t cr3 = 0;
            uint32_t frame = 0;
            int ok = 1;

            if (as_get((uint32_t)id, &cr3, &frame) != 0) {
                ok = 0;
            } else {
                uint32_t *directory =
                    (uint32_t *)(uintptr_t)cr3;

                uint32_t pde = directory[66];

                uint32_t *table =
                    (uint32_t *)(uintptr_t)
                    (pde & 0xFFFFF000U);

                uint32_t pte = table[0];

                if ((pde & 0x7U) == 0x7U &&
                    (pte & 0x7U) == 0x7U &&
                    (pte & 0xFFFFF000U) == frame) {
                    imprimir("Pagina privada USER: OK\n");
                } else {
                    ok = 0;
                }

                /* Las paginas del kernel no son USER */
                if ((directory[0] & 0x4U) == 0) {
                    imprimir("Directorio kernel supervisor: OK\n");
                } else {
                    ok = 0;
                }
            }

            if (as_destroy((uint32_t)id) != 0)
                ok = 0;

            if (pmm_free_frames() != before)
                ok = 0;

            if (ok)
                imprimir("USER MAP TEST: OK\n");
            else
                imprimir("USER MAP TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "userstacktest")) {
    imprimir("=== PRUEBA PILA DE USUARIO ===\n");

    if (!paging_cpu_enabled() || as_count() != 0) {
        imprimir("Activa pagingon y libera espacios anteriores.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int id = as_create();
        int ok = 1;

        if (id < 0) {
            imprimir("ERROR: crear espacio.\n");
        } else {
            uint32_t cr3 = 0;
            uint32_t data = 0;
            uint32_t stack = 0;

            if (as_get((uint32_t)id, &cr3, &data) != 0 ||
                as_get_user_stack((uint32_t)id, &stack) != 0) {
                ok = 0;
            } else {
                uint32_t *dir =
                    (uint32_t *)(uintptr_t)cr3;

                uint32_t pde = dir[66];

                uint32_t *table =
                    (uint32_t *)(uintptr_t)
                    (pde & 0xFFFFF000U);

                uint32_t data_pte = table[0];
                uint32_t stack_pte = table[1];

                if ((pde & 7U) != 7U ||
                    (data_pte & 7U) != 7U ||
                    (stack_pte & 7U) != 7U ||
                    (stack_pte & 0xFFFFF000U) != stack ||
                    data == stack) {
                    ok = 0;
                } else {
                    imprimir("Pagina datos USER: OK\n");
                    imprimir("Pagina pila USER: OK\n");
                    imprimir("Marcos independientes: OK\n");
                }

                if ((dir[0] & 4U) != 0)
                    ok = 0;
            }

            if (as_destroy((uint32_t)id) != 0)
                ok = 0;

            if (pmm_free_frames() != before)
                ok = 0;

            if (ok) {
                imprimir("Liberacion: OK\n");
                imprimir("USER STACK TEST: OK\n");
            } else {
                imprimir("USER STACK TEST: ERROR\n");
            }
        }
    }
}

else if (iguales(linea, "syscalltest")) {
    imprimir("=== PRUEBA SYSCALL MIOS ===\n");

    uint32_t antes = syscall_test_count();

    /*
     * Solicitar servicio numero 1.
     * Se ejecuta desde Ring 0 por ahora.
     */
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(MIOS_SYS_TEST)
        : "memory", "cc"
    );

    uint32_t despues = syscall_test_count();

    if (despues == antes + 1U) {
        imprimir("Interrupcion 0x80: OK\n");
        imprimir("Despachador del kernel: OK\n");
        imprimir("Retorno mediante IRETD: OK\n");
        imprimir("SYSCALL TEST: OK\n");
    } else {
        imprimir("SYSCALL TEST: ERROR\n");
    }
}

else if (iguales(linea, "usercodetest")) {
    imprimir("=== PROGRAMA DE USUARIO MIOS ===\n");

    if (!paging_cpu_enabled() || as_count() != 0) {
        imprimir("Activa pagingon y libera espacios previos.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int id = as_create();
        int ok = 1;

        if (id < 0) {
            imprimir("ERROR: crear espacio.\n");
        } else {
            uint32_t cr3 = 0;
            uint32_t frame = 0;

            if (as_get((uint32_t)id, &cr3, &frame) != 0) {
                ok = 0;
            } else {
                if (user_program_prepare(frame) != 0)
                    ok = 0;

                if (user_program_verify(frame) != 0)
                    ok = 0;

                if (ok) {
                    imprimir("Codigo x86 preparado: OK\n");
                    imprimir("Pagina privada USER: preparada\n");
                    imprimir("Direccion virtual: ");
                    imprimir_hex32(MIOS_USER_CODE_VADDR);
                    imprimir("\n");
                }
            }

            if (as_destroy((uint32_t)id) != 0)
                ok = 0;

            if (pmm_free_frames() != before)
                ok = 0;

            if (ok)
                imprimir("USER CODE TEST: OK\n");
            else
                imprimir("USER CODE TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "syscall2test")) {
    imprimir("=== PRUEBA DE SERVICIOS MIOS ===\n");

    uint32_t test_before = syscall_test_count();
    uint32_t exit_before = syscall_exit_count();

    /* Servicio 1: TEST */
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(MIOS_SYS_TEST)
        : "memory", "cc"
    );

    /* Servicio 2: EXIT provisional */
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(MIOS_SYS_EXIT)
        : "memory", "cc"
    );

    if (syscall_test_count() == test_before + 1U &&
        syscall_exit_count() == exit_before + 1U) {

        imprimir("SYS_TEST: OK\n");
        imprimir("SYS_EXIT solicitud: OK\n");
        imprimir("Interrupcion 0x80: OK\n");
        imprimir("SYSCALL 2 TEST: OK\n");
    } else {
        imprimir("SYSCALL 2 TEST: ERROR\n");
    }
}

else if (iguales(linea, "ring3test")) {
    imprimir("=== PRIMER PROGRAMA RING 3 ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0) {
        imprimir("Activa pagingon y libera espacios previos.\n");
    } else {
        uint32_t before = pmm_free_frames();

        int id = as_create();

        if (id < 0) {
            imprimir("ERROR: crear espacio.\n");
        } else {
            uint32_t user_cr3 = 0;
            uint32_t code_frame = 0;
            uint32_t stack_frame = 0;

            int ok = 1;

            if (as_get(
                    (uint32_t)id,
                    &user_cr3,
                    &code_frame
                ) != 0 ||
                as_get_user_stack(
                    (uint32_t)id,
                    &stack_frame
                ) != 0) {

                ok = 0;
            }

            if (ok &&
                user_program_prepare(code_frame) != 0) {
                ok = 0;
            }

            if (ok &&
                user_program_verify(code_frame) != 0) {
                ok = 0;
            }

            if (ok) {
                uint32_t tests_before =
                    syscall_test_count();

                uint32_t exits_before =
                    syscall_exit_count();
                    user_context_reset();
uint32_t ticks_before = timer_get_ticks();
                uint32_t old_esp0 =
                    tss_get_kernel_stack();

                /*
                 * Desactivar IRQ durante la
                 * prueba controlada.
                 */
                uint32_t flags = irq_save();

                /*
                 * Pila que usara la CPU al
                 * entrar desde Ring 3.
                 */
                uint32_t kernel_top =
                    (uint32_t)(uintptr_t)
                    (ring3_test_kernel_stack +
                     sizeof(ring3_test_kernel_stack));

                tss_set_kernel_stack(kernel_top);

                /*
                 * Entrada real a Ring 3.
                 *
                 * Debe regresar mediante
                 * SYS_EXIT y ring3_exit.
                 */
                ring3_enter(
                    user_cr3,
                    MIOS_AS_USER_STACK_TOP
                );

                /* Ya estamos nuevamente en Ring 0 */
                tss_set_kernel_stack(old_esp0);

                irq_restore(flags);
                
uint32_t ticks_after = timer_get_ticks();

if ((uint32_t)(ticks_after - ticks_before) > 0U) {
    imprimir("IRQ0 durante Ring 3: OK\n");
} else {
    imprimir("IRQ0 durante Ring 3: NO DETECTADA\n");
    ok = 0;
}

/* Verificar el contexto registrado por IRQ0 */
if (user_context_count() > 0U &&
    user_context_verify(user_cr3)) {

    imprimir("Contexto Ring 3 capturado: OK\n");
    imprimir("CS y SS de usuario: OK\n");
    imprimir("EIP y ESP de usuario: OK\n");
    imprimir("CR3 del proceso: OK\n");

} else {
    imprimir("USER CONTEXT TEST: ERROR\n");
    ok = 0;
}



                if (syscall_test_count() !=
                    tests_before + 1U) {
                    ok = 0;
                }

                if (syscall_exit_count() !=
                    exits_before + 1U) {
                    ok = 0;
                }

                if (ok) {
                    imprimir("SYS_TEST desde usuario: OK\n");
                    imprimir("SYS_EXIT desde usuario: OK\n");
                    imprimir("Retorno a Ring 0: OK\n");
                }
            }

            if (as_destroy((uint32_t)id) != 0)
                ok = 0;

            if (pmm_free_frames() != before)
                ok = 0;

            if (ok) {
                imprimir("RING 3 TEST: OK\n");
            } else {
                imprimir("RING 3 TEST: ERROR\n");
            }
        }
    }
}

else if (iguales(linea, "ring3verify")) {
    imprimir("=== VERIFICACION CPL MIOS ===\n");

    uint32_t ring0_before = syscall_ring0_count();
    uint32_t ring3_before = syscall_ring3_count();

    /* Una llamada desde la shell Ring 0 */
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(MIOS_SYS_TEST)
        : "memory", "cc"
    );

    uint32_t ring0_after = syscall_ring0_count();
    uint32_t ring3_after = syscall_ring3_count();

    if (ring0_after == ring0_before + 1U &&
        ring3_after == ring3_before) {

        imprimir("Origen Ring 0: OK\n");
        imprimir("Clasificacion CPL: OK\n");
    } else {
        imprimir("RING 3 VERIFY: ERROR\n");
        return;
    }

    /*
     * Comprobar si ya se ejecutaron
     * syscalls reales desde Ring 3.
     */
    if (ring3_after >= 2U) {
        imprimir("Syscalls Ring 3 registradas: OK\n");
        imprimir("RING 3 VERIFY: OK\n");
    } else {
        imprimir("Falta ejecutar ring3test.\n");
    }
}

else if (iguales(linea, "usertasktest")) {
    imprimir("=== DOS TAREAS RING 3 ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon.\n");
    } else if (as_count() != 0U) {
        imprimir("ERROR: hay espacios activos.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        if (user_tasks_prepare() != 0) {
            imprimir("ERROR: preparando tareas.\n");
            ok = 0;
        } else {
            
if (user_tasks_verify() != 0) {
    imprimir("ERROR: verificando tareas.\n");
    ok = 0;
} else {
    imprimir("Tarea 1 preparada: OK\n");
    imprimir("Tarea 2 preparada: OK\n");
    imprimir("CR3 independientes: OK\n");
    imprimir("Pilas USER independientes: OK\n");
    imprimir("Pilas kernel independientes: OK\n");
    imprimir("Programas cargados: OK\n");

    if (user_tasks_context_verify() == 0) {
        imprimir("Contexto inicial tarea 0: OK\n");
        imprimir("Contexto inicial tarea 1: OK\n");
        imprimir("Registros y segmentos: OK\n");
        imprimir("Marcos IRETD independientes: OK\n");
    } else {
        imprimir("ERROR: contextos iniciales.\n");
        ok = 0;
    }
}

        }

        if (user_tasks_cleanup() != 0) {
            imprimir("ERROR: liberando tareas.\n");
            ok = 0;
        }

        if (as_count() != 0U ||
            pmm_free_frames() != before) {
            imprimir("ERROR: memoria no recuperada.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER TASK TEST: OK\n");
        } else {
            imprimir("USER TASK TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "userrrtest")) {
    imprimir("=== ROUND ROBIN DE USUARIO ===\n");

    if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon.\n");
    } else if (as_count() != 0U) {
        imprimir("ERROR: hay espacios activos.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        /* Crear las dos tareas */
        if (user_tasks_prepare() != 0) {
            imprimir("ERROR: preparando tareas.\n");
            ok = 0;
        } else {
            if (user_tasks_verify() != 0) {
                imprimir("ERROR: tareas invalidas.\n");
                ok = 0;
            } else {
                imprimir("Dos tareas READY: OK\n");

                /* Probar la seleccion Round Robin */
                if (user_rr_selftest() == 0) {
                    imprimir("Alternancia 0-1-0-1: OK\n");
                    imprimir("Conteo de cambios: OK\n");
                    imprimir("Planificador usuario: OK\n");
                } else {
                    imprimir("ERROR: Round Robin.\n");
                    ok = 0;
                }
            }
        }

        /* Liberar los espacios creados */
        if (user_tasks_cleanup() != 0) {
            imprimir("ERROR: limpieza.\n");
            ok = 0;
        }

        if (as_count() != 0U ||
            pmm_free_frames() != before) {
            imprimir("ERROR: memoria no recuperada.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER RR TEST: OK\n");
        } else {
            imprimir("USER RR TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "usercontextswitchtest")) {
    imprimir("=== CONTEXTOS PARA ROUND ROBIN ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U) {
        imprimir("Activa pagingon y libera espacios.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        if (user_tasks_prepare() != 0) {
            ok = 0;
        } else {
            if (user_tasks_verify() != 0 ||
                user_tasks_context_verify() != 0) {
                ok = 0;
            }

            if (ok) {
                for (uint32_t i = 0; i < 2U; i++) {
                    const UserTask *task =
                        user_task_get(i);

                    uint32_t esp = 0;
                    uint32_t cr3 = 0;
                    uint32_t esp0 = 0;

                    if (task == 0 ||
                        user_task_get_context(
                            i, &esp, &cr3, &esp0
                        ) != 0 ||
                        esp != task->saved_kernel_esp ||
                        cr3 != task->cr3 ||
                        esp0 != task->kernel_esp0 ||
                        user_task_save_context(
                            i, esp
                        ) != 0) {
                        ok = 0;
                        break;
                    }
                }
            }
        }

        if (ok) {
            imprimir("Contexto tarea 0: OK\n");
            imprimir("Contexto tarea 1: OK\n");
            imprimir("ESP guardados: OK\n");
            imprimir("CR3 independientes: OK\n");
            imprimir("ESP0 independientes: OK\n");
        }

        if (user_tasks_cleanup() != 0)
            ok = 0;

        if (as_count() != 0U ||
            pmm_free_frames() != before)
            ok = 0;

        if (ok)
            imprimir("USER CONTEXT SWITCH TEST: OK\n");
        else
            imprimir("USER CONTEXT SWITCH TEST: ERROR\n");
    }
}

else if (iguales(linea, "userframecheck")) {
    imprimir("=== VALIDACION DE MARCOS USER ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U) {
        imprimir("Activa pagingon y libera espacios.\n");
    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        if (user_tasks_prepare() != 0) {
            ok = 0;
        } else {
            if (user_tasks_context_verify() != 0)
                ok = 0;

            for (uint32_t i = 0;
                 i < MIOS_USER_TASK_COUNT;
                 i++) {

                const UserTask *task =
                    user_task_get(i);

                if (task == 0) {
                    ok = 0;
                    break;
                }

                uint32_t esp =
                    task->saved_kernel_esp;

                /* Contexto correcto */
                if (user_task_validate_context(
                        i, esp
                    ) != 0) {
                    ok = 0;
                }

                /* Direccion desalineada */
                if (user_task_validate_context(
                        i, esp + 1U
                    ) == 0) {
                    ok = 0;
                }

                /* Marco fuera de la pila */
                if (user_task_validate_context(
                        i, task->kernel_esp0
                    ) == 0) {
                    ok = 0;
                }

                if (user_task_save_context(
                        i, esp
                    ) != 0) {
                    ok = 0;
                }
            }

            /* La pila de otra tarea no es valida */
            const UserTask *other =
                user_task_get(1);

            if (other != 0 &&
                user_task_validate_context(
                    0,
                    other->saved_kernel_esp
                ) == 0) {
                ok = 0;
            }
        }

        if (ok) {
            imprimir("Marcos Ring 3 validos: OK\n");
            imprimir("Limites de pila kernel: OK\n");
            imprimir("ESP desalineado rechazado: OK\n");
            imprimir("Contexto de otra tarea rechazado: OK\n");
        }

        if (user_tasks_cleanup() != 0)
            ok = 0;

        if (as_count() != 0U ||
            pmm_free_frames() != before)
            ok = 0;

        if (ok)
            imprimir("USER FRAME CHECK: OK\n");
        else
            imprimir("USER FRAME CHECK: ERROR\n");
    }
}

else if (iguales(linea, "usermultitest")) {
    imprimir("=== MULTITAREA REAL RING 3 ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U) {

        imprimir("Activa pagingon y libera espacios.\n");

    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        if (user_tasks_prepare() != 0) {
            imprimir("ERROR: preparando tareas.\n");
            ok = 0;
        } else {
            if (user_runtime_start() != 0) {
                imprimir("ERROR: iniciando planificador.\n");
                ok = 0;
            } else {
                const UserTask *first =
                    user_task_get(0);

                if (first == 0) {
                    ok = 0;
                } else {
                    uint32_t test_before =
                        syscall_test_count();

                    uint32_t exit_before =
                        syscall_exit_count();

                    uint32_t old_esp0 =
                        tss_get_kernel_stack();

                    uint32_t flags = irq_save();

                    /* Pila privilegiada de tarea 0 */
                    tss_set_kernel_stack(
                        first->kernel_esp0
                    );

                    /*
                     * Inicia la tarea 0.
                     * IRQ0 se encargara de alternar
                     * entre las dos tareas.
                     */
                    ring3_enter(
                        first->cr3,
                        first->user_esp
                    );

                    /*
                     * Volvemos aqui cuando ambas
                     * tareas ejecutaron SYS_EXIT.
                     */
                    tss_set_kernel_stack(old_esp0);
                    irq_restore(flags);

                    if (user_runtime_error() != 0) {
                        imprimir("ERROR: cambio de contexto.\n");
                        ok = 0;
                    }

                    if (syscall_test_count() !=
                        test_before + 2U) {
                        imprimir("ERROR: SYS_TEST.\n");
                        ok = 0;
                    }

                    if (syscall_exit_count() !=
                        exit_before + 2U) {
                        imprimir("ERROR: SYS_EXIT.\n");
                        ok = 0;
                    }
                    
/* Verificar la finalizacion individual */
const UserTask *task0 = user_task_get(0);
const UserTask *task1 = user_task_get(1);

if (task0 == 0 || task1 == 0 ||
    task0->state != USER_TASK_DONE ||
    task1->state != USER_TASK_DONE) {

    imprimir("ERROR: estados de tareas.\n");
    ok = 0;
} else {
    imprimir("Estados DONE de ambas tareas: OK\n");
}

/* Cada tarea debe ejecutar SYS_EXIT una vez */
if (user_runtime_task_exits(0) != 1U ||
    user_runtime_task_exits(1) != 1U ||
    user_runtime_total_exits() != 2U) {

    imprimir("ERROR: conteo de finalizaciones.\n");
    ok = 0;
} else {
    imprimir("Finalizacion tarea 0: OK\n");
    imprimir("Finalizacion tarea 1: OK\n");
    imprimir("SYS_EXIT sin duplicados: OK\n");
}


                    if (user_runtime_irq_switches() == 0U) {
                        imprimir("ERROR: sin cambios por IRQ0.\n");
                        ok = 0;
                    }

                    if (user_runtime_active()) {
                        imprimir("ERROR: tareas activas.\n");
                        ok = 0;
                    }

                    if (ok) {
                        imprimir("Dos programas Ring 3: OK\n");
                        imprimir("Round Robin por IRQ0: OK\n");
                        imprimir("Dos SYS_TEST: OK\n");
                        imprimir("Dos SYS_EXIT: OK\n");
                        imprimir("Retorno a la shell: OK\n");
                    }
                }
            }
        }

        /*
         * Solo liberar despues de volver
         * al directorio principal.
         */
        
/*
 * Antes de destruir recursos,
 * comprobar que volvimos al
 * directorio principal del kernel.
 */
uint32_t current_cr3 = 0;

__asm__ volatile (
    "mov %%cr3, %0"
    : "=r"(current_cr3)
    :
    : "memory"
);

current_cr3 &= 0xFFFFF000U;

if (user_runtime_active() ||
    current_cr3 != paging_directory_address()) {

    imprimir("ERROR: retorno inseguro al kernel.\n");
    ok = 0;

    /*
     * No liberar marcos potencialmente
     * utilizados por la CPU.
     */
} else {
    imprimir("CR3 principal restaurado: OK\n");

    if (user_tasks_cleanup() != 0) {
        imprimir("ERROR: limpieza de tareas.\n");
        ok = 0;
    }
}


        if (as_count() != 0U ||
            pmm_free_frames() != before) {
            imprimir("ERROR: memoria no recuperada.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER MULTI TEST: OK\n");
        } else {
            imprimir("USER MULTI TEST: ERROR\n");
        }
    }
}


else if (iguales(linea, "userproctest")) {
    imprimir("=== PROCESOS DE USUARIO MIOS ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U ||
        user_process_count() != 0U) {

        imprimir("Activa pagingon y libera espacios.\n");

    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;

        if (user_process_prepare() != 0) {
            imprimir("ERROR: creando procesos.\n");
            ok = 0;
        } else {
            if (user_process_verify() != 0) {
                imprimir("ERROR: verificando procesos.\n");
                ok = 0;
            } else {
                imprimir("Dos PID asignados: OK\n");
                imprimir("Procesos registrados: OK\n");
                imprimir("CR3 independientes: OK\n");
                imprimir("Asociacion PID - tarea: OK\n");
            }
        }

        if (user_process_cleanup() != 0) {
            imprimir("ERROR: limpieza de procesos.\n");
            ok = 0;
        }

        if (user_process_count() != 0U ||
            as_count() != 0U ||
            pmm_free_frames() != before) {

            imprimir("ERROR: recursos pendientes.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Tabla de procesos liberada: OK\n");
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER PROC TEST: OK\n");
        } else {
            imprimir("USER PROC TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "userpsdemo")) {
    imprimir("=== TABLA PROCESOS USER ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U ||
        user_process_count() != 0U) {

        imprimir("Activa pagingon y libera espacios.\n");

    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;
        int prepared = 0;

        if (user_process_prepare() != 0) {
            imprimir("ERROR: creando procesos.\n");
            ok = 0;
        } else {
            prepared = 1;

            if (user_process_verify() != 0 ||
                user_process_sync_states() != 0) {
                ok = 0;
            }

            if (ok) {
                for (uint32_t i = 0;
                     i < MIOS_USER_TASK_COUNT;
                     i++) {

                    UserProcess p;

                    if (user_process_get_at(i, &p) != 0 ||
                        p.state != USER_PROCESS_READY) {
                        ok = 0;
                        break;
                    }

                    imprimir("PID: ");
                    imprimir_hex32(p.pid);

                    imprimir(" | CR3: ");
                    imprimir_hex32(p.cr3);

                    imprimir(" | READY\n");
                }
            }

            /*
             * Simular la finalizacion de
             * la primera tarea.
             */
            if (ok &&
                user_task_mark_done(0) != 0) {
                ok = 0;
            }

            if (ok &&
                user_process_sync_states() != 0) {
                ok = 0;
            }

            if (ok) {
                UserProcess p0;
                UserProcess p1;

                uint32_t pid0 = 0;
                uint32_t pid1 = 0;

                if (user_process_get_at(0, &p0) != 0 ||
                    user_process_get_at(1, &p1) != 0 ||
                    user_process_pid_by_task(
                        0, &pid0
                    ) != 0 ||
                    user_process_pid_by_task(
                        1, &pid1
                    ) != 0 ||
                    p0.state != USER_PROCESS_DONE ||
                    p1.state != USER_PROCESS_READY ||
                    p0.pid != pid0 ||
                    p1.pid != pid1 ||
                    pid0 == pid1) {

                    ok = 0;
                } else {
                    imprimir("Proceso 0 -> DONE: OK\n");
                    imprimir("Proceso 1 -> READY: OK\n");
                    imprimir("Asociacion PID-tarea: OK\n");
                }
            }
        }

        /*
         * Liberar los recursos creados.
         * La limpieza tambien acepta tareas
         * marcadas como DONE.
         */
        if (prepared ||
            user_process_count() != 0U ||
            as_count() != 0U) {

            if (user_process_cleanup() != 0) {
                imprimir("ERROR: limpieza.\n");
                ok = 0;
            }
        }

        if (user_process_count() != 0U ||
            as_count() != 0U ||
            pmm_free_frames() != before) {
            imprimir("ERROR: recursos pendientes.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER PS DEMO: OK\n");
        } else {
            imprimir("USER PS DEMO: ERROR\n");
        }
    }
}

else if (iguales(linea, "userpidtest")) {
    imprimir("=== PROCESOS RING 3 CON PID ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U ||
        user_process_count() != 0U) {

        imprimir("ERROR: ejecuta pagingon y libera espacios.\n");

    } else {
        uint32_t before = pmm_free_frames();
        int ok = 1;
        int prepared = 0;
        int started = 0;

        if (user_process_prepare() != 0) {
            imprimir("ERROR: creando procesos.\n");
            ok = 0;
        } else {
            prepared = 1;

            if (user_process_verify() != 0) {
                imprimir("ERROR: tabla PID.\n");
                ok = 0;
            }
        }

        if (ok) {
            UserProcess p0;
            UserProcess p1;

            if (user_process_get_at(0, &p0) != 0 ||
                user_process_get_at(1, &p1) != 0 ||
                p0.pid == p1.pid ||
                p0.cr3 == p1.cr3) {

                ok = 0;
            } else {
                imprimir("Dos PID independientes: OK\n");
            }
        }

        if (ok) {
            if (user_runtime_start() != 0) {
                imprimir("ERROR: iniciando planificador.\n");
                ok = 0;
            } else {
                started = 1;
                syscall_audit_reset();
            }
        }

        if (ok) {
            const UserTask *first =
                user_task_get(0);

            uint32_t active_pid = 0;

            if (first == NULL ||
                user_process_current_pid(
                    &active_pid
                ) != 0) {

                imprimir("ERROR: PID activo.\n");
                ok = 0;

            } else {
                UserProcess p0;

                if (user_process_get_at(0, &p0) != 0 ||
                    active_pid != p0.pid) {

                    imprimir("ERROR: PID inicial.\n");
                    ok = 0;

                } else {
                    imprimir("PID inicial asociado: OK\n");

                    uint32_t test_before =
                        syscall_test_count();

                    uint32_t exit_before =
                        syscall_exit_count();

                    uint32_t old_esp0 =
                        tss_get_kernel_stack();

                    uint32_t flags = irq_save();

                    tss_set_kernel_stack(
                        first->kernel_esp0
                    );

                    /*
                     * Ejecucion real de Ring 3.
                     * El temporizador alterna
                     * las dos tareas.
                     */
                    ring3_enter(
                        first->cr3,
                        first->user_esp
                    );

                    /* Regreso a la shell Ring 0 */
                    tss_set_kernel_stack(old_esp0);
                    irq_restore(flags);

                    if (user_runtime_error() != 0 ||
                        user_runtime_active()) {

                        imprimir("ERROR: ejecucion.\n");
                        ok = 0;
                    }

                    if (syscall_test_count() !=
                        test_before + 2U) {

                        imprimir("ERROR: SYS_TEST.\n");
                        ok = 0;
                    }

                    if (syscall_exit_count() !=
                        exit_before + 2U) {

                        imprimir("ERROR: SYS_EXIT.\n");
                        ok = 0;
                    }

                    if (user_runtime_total_exits() != 2U ||
                        user_runtime_task_exits(0) != 1U ||
                        user_runtime_task_exits(1) != 1U) {

                        imprimir("ERROR: finalizaciones.\n");
                        ok = 0;
                    }
                    
/*
 * Comprobar las syscalls realizadas
 * por cada uno de los dos procesos.
 */
SyscallAudit audit0;
SyscallAudit audit1;

UserProcess process0;
UserProcess process1;

if (syscall_audit_get(0, &audit0) != 0 ||
    syscall_audit_get(1, &audit1) != 0 ||
    user_process_get_at(0, &process0) != 0 ||
    user_process_get_at(1, &process1) != 0) {

    imprimir("ERROR: consultando auditoria.\n");
    ok = 0;

} else if (
    audit0.pid != process0.pid ||
    audit1.pid != process1.pid ||
    audit0.pid == audit1.pid ||
    audit0.test_calls != 1U ||
    audit1.test_calls != 1U ||
    audit0.exit_calls != 1U ||
    audit1.exit_calls != 1U
) {

    imprimir("ERROR: syscalls por PID.\n");
    ok = 0;

} else {
    imprimir("SYS_TEST del proceso 0: OK\n");
    imprimir("SYS_EXIT del proceso 0: OK\n");

    imprimir("SYS_TEST del proceso 1: OK\n");
    imprimir("SYS_EXIT del proceso 1: OK\n");

    imprimir("Identidad PID en syscalls: OK\n");
}


                    if (user_runtime_irq_switches() == 0U) {
                        imprimir("ERROR: sin alternancia IRQ0.\n");
                        ok = 0;
                    }
                }
            }
        }

        /*
         * Sincronizar los estados despues
         * de terminar la ejecucion.
         */
        if (ok && user_process_sync_states() != 0)
            ok = 0;

        if (ok) {
            UserProcess p0;
            UserProcess p1;

            if (user_process_get_at(0, &p0) != 0 ||
                user_process_get_at(1, &p1) != 0 ||
                p0.state != USER_PROCESS_DONE ||
                p1.state != USER_PROCESS_DONE) {

                imprimir("ERROR: estados de procesos.\n");
                ok = 0;

            } else {
                imprimir("Proceso PID 1 finalizado: OK\n");
                imprimir("Proceso PID 2 finalizado: OK\n");
                imprimir("Estados DONE sincronizados: OK\n");
            }
        }

        /*
         * Comprobar CR3 antes de liberar.
         */
        uint32_t current_cr3 = 0;

        __asm__ volatile (
            "mov %%cr3, %0"
            : "=r"(current_cr3)
            :
            : "memory"
        );

        current_cr3 &= 0xFFFFF000U;

        if (current_cr3 !=
            paging_directory_address()) {

            imprimir("ERROR: CR3 principal no restaurado.\n");
            ok = 0;

        } else if (user_runtime_active()) {

            imprimir("ERROR: sesion todavia activa.\n");
            ok = 0;

        } else {
            if (prepared ||
                user_process_count() != 0U ||
                as_count() != 0U) {

                if (user_process_cleanup() != 0) {
                    imprimir("ERROR: limpieza.\n");
                    ok = 0;
                }
            }
        }

        if (user_process_count() != 0U ||
            as_count() != 0U ||
            pmm_free_frames() != before) {

            imprimir("ERROR: recursos pendientes.\n");
            ok = 0;
        }

        if (ok && started) {
            imprimir("Round Robin por IRQ0: OK\n");
            imprimir("Dos SYS_EXIT asociados: OK\n");
            imprimir("Memoria recuperada: OK\n");
            imprimir("USER PID TEST: OK\n");
        } else {
            imprimir("USER PID TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "elftest")) {
    imprimir("=== VALIDADOR ELF32 MIOS ===\n");

    if (sizeof(Elf32_Ehdr) != 52U ||
        sizeof(Elf32_Phdr) != 32U) {

        imprimir("ERROR: estructuras ELF32.\n");

    } else if (elf32_selftest() == 0) {

        imprimir("Cabecera ELF32: OK\n");
        imprimir("Arquitectura Intel x86: OK\n");
        imprimir("Direccion de entrada: OK\n");
        imprimir("Tamano de cabeceras: OK\n");
        imprimir("Archivos invalidos rechazados: OK\n");
        imprimir("ELF TEST: OK\n");

    } else {
        imprimir("ELF TEST: ERROR\n");
    }
}

else if (iguales(linea, "elfsegmenttest")) {
    imprimir("=== SEGMENTOS ELF32 ===\n");

    if (elf32_segment_selftest() == 0) {
        imprimir("Segmento PT_LOAD: OK\n");
        imprimir("Limites de archivo: OK\n");
        imprimir("Memoria solicitada: OK\n");
        imprimir("Direccion de entrada: OK\n");
        imprimir("Archivos invalidos rechazados: OK\n");
        imprimir("ELF SEGMENT TEST: OK\n");
    } else {
        imprimir("ELF SEGMENT TEST: ERROR\n");
    }
}


else if (iguales(linea, "elfloadtest")) {
    imprimir("=== CARGADOR ELF32 MIOS ===\n");

    if (!paging_cpu_enabled() ||
        as_count() != 0U) {

        imprimir("Activa pagingon y libera espacios.\n");

    } else {
        uint32_t before = pmm_free_frames();

        int ok = elf32_load_selftest() == 0;

        if (ok)
            imprimir("Segmento PT_LOAD copiado: OK\n");

        if (ok)
            imprimir("Direccion de entrada: OK\n");

        if (ok)
            imprimir("Memoria BSS inicializada: OK\n");

        if (as_count() != 0U ||
            pmm_free_frames() != before) {
            ok = 0;
        }

        if (ok) {
            imprimir("Memoria recuperada: OK\n");
            imprimir("ELF LOAD TEST: OK\n");
        } else {
            imprimir("ELF LOAD TEST: ERROR\n");
        }
    }
}

else if (iguales(linea, "elfruntest")) {
    imprimir("=== EJECUTANDO ELF32 REAL ===\n");

    if (!paging_cpu_enabled() || as_count() != 0U) {
        imprimir("Activa pagingon y libera espacios.\n");
    } else {
        const uint8_t *image =
            _binary_user_hola_elf_start;

        uint32_t image_size =
            (uint32_t)(
                _binary_user_hola_elf_end -
                _binary_user_hola_elf_start
            );

        uint32_t before = pmm_free_frames();
        int ok = 1;
        int id = -1;

        Elf32_LoadInfo info;
        uint32_t entry = 0;
        uint32_t user_cr3 = 0;
        uint32_t code_frame = 0;
        uint32_t stack_frame = 0;

        /* Validar el ELF real */
        if (elf32_validate_segments(
                image, image_size, &info
            ) != 0) {
            imprimir("ERROR: ELF invalido.\n");
            ok = 0;
        }

        /*
         * El lanzador actual ring3_enter
         * comienza en 0x10800000.
         */
        

        if (ok) {
            id = as_create();

            if (id < 0) {
                imprimir("ERROR: crear espacio.\n");
                ok = 0;
            }
        }

        if (ok &&
            (as_get(
                (uint32_t)id,
                &user_cr3,
                &code_frame
            ) != 0 ||
             as_get_user_stack(
                (uint32_t)id,
                &stack_frame
            ) != 0)) {

            imprimir("ERROR: memoria de usuario.\n");
            ok = 0;
        }

        if (ok &&
            elf32_load(
                image,
                image_size,
                (uint32_t)id,
                &entry
            ) != 0) {

            imprimir("ERROR: cargando ELF.\n");
            ok = 0;
        }

        
/*
 * Esta prueba utiliza un ELF cuyo
 * punto de entrada esta desplazado.
 */
if (ok && entry !=
        MIOS_ELF_USER_BASE + 16U) {
    imprimir("ERROR: entrada ELF inesperada.\n");
    ok = 0;
}


        if (ok) {
            imprimir("Archivo ELF32 validado: OK\n");
            imprimir("Segmentos cargados: OK\n");

            uint32_t tests_before =
                syscall_test_count();

            uint32_t exits_before =
                syscall_exit_count();

            uint32_t old_esp0 =
                tss_get_kernel_stack();

            uint32_t flags = irq_save();

            uint32_t kernel_top =
                (uint32_t)(uintptr_t)(
                    elf_test_kernel_stack +
                    sizeof(elf_test_kernel_stack)
                );

            tss_set_kernel_stack(kernel_top);

            /*
             * Ejecutar el ELF cargado.
             * SYS_EXIT devuelve a Ring 0.
             */
           
/* Ejecutar desde el e_entry del ELF */
ring3_enter_at(
    user_cr3,
    MIOS_AS_USER_STACK_TOP,
    entry
);


            /* De vuelta en el kernel */
            tss_set_kernel_stack(old_esp0);
            irq_restore(flags);

            if (syscall_test_count() !=
                tests_before + 1U) {
                imprimir("ERROR: SYS_TEST del ELF.\n");
                ok = 0;
            }

            if (syscall_exit_count() !=
                exits_before + 1U) {
                imprimir("ERROR: SYS_EXIT del ELF.\n");
                ok = 0;
            }

            if (ok) {
                imprimir("SYS_TEST desde ELF: OK\n");
                imprimir("SYS_EXIT desde ELF: OK\n");
            }
        }

        /* Confirmar regreso al CR3 principal */
        uint32_t current_cr3 = 0;

        __asm__ volatile (
            "mov %%cr3, %0"
            : "=r"(current_cr3)
            :
            : "memory"
        );

        current_cr3 &= 0xFFFFF000U;

        if (current_cr3 !=
            paging_directory_address()) {
            imprimir("ERROR: CR3 no restaurado.\n");
            ok = 0;
        } else if (id >= 0) {
            if (as_destroy((uint32_t)id) != 0) {
                imprimir("ERROR: liberar espacio.\n");
                ok = 0;
            }
        }

        if (as_count() != 0U ||
            pmm_free_frames() != before) {
            imprimir("ERROR: memoria pendiente.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Retorno a la shell: OK\n");
            imprimir("Memoria recuperada: OK\n");
            imprimir("ELF RUN TEST: OK\n");
        } else {
            imprimir("ELF RUN TEST: ERROR\n");
        }
    }
}


else if (iguales(linea, "elfdisktest")) {
    imprimir("=== LECTURA ELF DESDE ATA ===\n");

    uint32_t size = 0;
    int ok = 1;

    if (elf_disk_read(
            elf_disk_buffer,
            sizeof(elf_disk_buffer),
            &size
        ) != 0) {

        imprimir("ERROR: lectura binaria ATA.\n");
        ok = 0;
    }

    Elf32_LoadInfo info;

    if (ok &&
        elf32_validate_segments(
            elf_disk_buffer,
            size,
            &info
        ) != 0) {

        imprimir("ERROR: ELF invalido.\n");
        ok = 0;
    }

    /*
     * Comparacion con el ELF embebido
     * que ya ejecutamos anteriormente.
     */
    if (ok) {
        uint32_t expected_size =
            (uint32_t)(
                _binary_user_hola_elf_end -
                _binary_user_hola_elf_start
            );

        if (size != expected_size) {
            ok = 0;
        } else {
            for (uint32_t i = 0; i < size; i++) {
                if (elf_disk_buffer[i] !=
                    _binary_user_hola_elf_start[i]) {
                    ok = 0;
                    break;
                }
            }
        }
    }

    if (ok) {
        imprimir("Lectura multisector ATA: OK\n");
        imprimir("Tamano ELF completo: OK\n");
        imprimir("Bytes binarios verificados: OK\n");
        imprimir("Segmento PT_LOAD: OK\n");
        imprimir("ELF DISK TEST: OK\n");
    } else {
        imprimir("ELF DISK TEST: ERROR\n");
    }
}

else if (iguales(linea, "elfdiskrun")) {
    imprimir("=== EJECUTAR ELF DESDE ATA ===\n");

    if (!paging_cpu_enabled() || as_count() != 0U) {
        imprimir("ERROR: ejecuta pagingon y libera espacios.\n");
    } else {
        int ok = 1;
        int id = -1;

        uint32_t size = 0;
        uint32_t entry = 0;
        uint32_t user_cr3 = 0;
        uint32_t code_frame = 0;
        uint32_t stack_frame = 0;

        uint32_t before = pmm_free_frames();

        Elf32_LoadInfo info;

        /* 1. Leer el ELF completo desde ATA */
        if (elf_disk_read(
                elf_disk_buffer,
                sizeof(elf_disk_buffer),
                &size
            ) != 0) {

            imprimir("ERROR: lectura ELF desde ATA.\n");
            ok = 0;
        } else {
            imprimir("Lectura ELF desde ATA: OK\n");
        }

        /* 2. Validar segmentos y punto de entrada */
        if (ok && elf32_validate_segments(
                elf_disk_buffer,
                size,
                &info
            ) != 0) {

            imprimir("ERROR: segmentos ELF.\n");
            ok = 0;
        } else if (ok) {
            imprimir("Segmentos ELF32: OK\n");
        }

        /* 3. Crear memoria privada del programa */
        if (ok) {
            id = as_create();

            if (id < 0) {
                imprimir("ERROR: espacio USER.\n");
                ok = 0;
            }
        }

        /* 4. Consultar CR3 y marcos fisicos */
        if (ok &&
            (as_get(
                (uint32_t)id,
                &user_cr3,
                &code_frame
            ) != 0 ||
             as_get_user_stack(
                (uint32_t)id,
                &stack_frame
            ) != 0)) {

            imprimir("ERROR: obtener memoria USER.\n");
            ok = 0;
        }

        /* 5. Cargar el ELF leido desde ATA */
        if (ok && elf32_load(
                elf_disk_buffer,
                size,
                (uint32_t)id,
                &entry
            ) != 0) {

            imprimir("ERROR: cargar ELF.\n");
            ok = 0;
        } else if (ok) {
            imprimir("ELF cargado en memoria USER: OK\n");
        }

        if (ok && entry != info.entry) {
            imprimir("ERROR: direccion de entrada.\n");
            ok = 0;
        }

        /* 6. Ejecutar realmente en Ring 3 */
        if (ok) {
            uint32_t test_before =
                syscall_test_count();

            uint32_t exit_before =
                syscall_exit_count();

            uint32_t old_esp0 =
                tss_get_kernel_stack();

            uint32_t flags = irq_save();

            uint32_t kernel_top =
                (uint32_t)(uintptr_t)(
                    elf_disk_run_kernel_stack +
                    sizeof(elf_disk_run_kernel_stack)
                );

            tss_set_kernel_stack(kernel_top);

            /*
             * Se ejecuta el codigo obtenido
             * directamente desde el disco.
             */
            ring3_enter_at(
                user_cr3,
                MIOS_AS_USER_STACK_TOP,
                entry
            );

            /* El SYS_EXIT devuelve a Ring 0 */
            tss_set_kernel_stack(old_esp0);
            irq_restore(flags);

            if (syscall_test_count() !=
                test_before + 1U) {

                imprimir("ERROR: SYS_TEST.\n");
                ok = 0;
            }

            if (syscall_exit_count() !=
                exit_before + 1U) {

                imprimir("ERROR: SYS_EXIT.\n");
                ok = 0;
            }

            if (ok) {
                imprimir("SYS_TEST desde ELF ATA: OK\n");
                imprimir("SYS_EXIT desde ELF ATA: OK\n");
            }
        }

        /* 7. Comprobar que regresamos al kernel */
        uint32_t current_cr3 = 0;

        __asm__ volatile (
            "mov %%cr3, %0"
            : "=r"(current_cr3)
            :
            : "memory"
        );

        current_cr3 &= 0xFFFFF000U;

        if (current_cr3 !=
            paging_directory_address()) {

            imprimir("ERROR: CR3 principal.\n");
            ok = 0;

            /* No destruir el espacio activo */

        } else if (id >= 0) {

            if (as_destroy((uint32_t)id) != 0) {
                imprimir("ERROR: liberar espacio.\n");
                ok = 0;
            }
        }

        if (as_count() != 0U ||
            pmm_free_frames() != before) {

            imprimir("ERROR: memoria pendiente.\n");
            ok = 0;
        }

        if (ok) {
            imprimir("Retorno a la shell: OK\n");
            imprimir("Memoria recuperada: OK\n");
            imprimir("ELF DISK RUN: OK\n");
        } else {
            imprimir("ELF DISK RUN: ERROR\n");
        }
    }
}

else if (iguales(linea, "binfstest")) {
    imprimir("=== ARCHIVOS BINARIOS MIOS ===\n");

    uint32_t size = 0;
    int ok = 1;

    if (binfs_read(
            "hola.elf",
            binfs_test_buffer,
            sizeof(binfs_test_buffer),
            &size
        ) != 0) {

        imprimir("ERROR: buscar hola.elf.\n");
        ok = 0;
    }

    Elf32_LoadInfo info;

    if (ok &&
        elf32_validate_segments(
            binfs_test_buffer,
            size,
            &info
        ) != 0) {

        imprimir("ERROR: validar ELF.\n");
        ok = 0;
    }

    /* Verificar que no encuentre otro nombre */
    uint32_t missing_size = 0;

    if (binfs_read(
            "inexistente.elf",
            binfs_test_buffer,
            sizeof(binfs_test_buffer),
            &missing_size
        ) == 0) {

        imprimir("ERROR: busqueda por nombre.\n");
        ok = 0;
    }

    if (ok) {
        imprimir("Archivo hola.elf encontrado: OK\n");
        imprimir("Tamano binario: OK\n");
        imprimir("Lectura de sectores: OK\n");
        imprimir("Busqueda por nombre: OK\n");
        imprimir("ELF32 validado: OK\n");
        imprimir("BINFS TEST: OK\n");
    } else {
        imprimir("BINFS TEST: ERROR\n");
    }
}


else if (iguales(linea, "run")) {

    /* La shell ya separo los argumentos */
    const char *filename = args;

    /* Omitir espacios adicionales */
    while (*filename == ' ') {
        filename++;
    }

    if (*filename == '\0') {
        imprimir("Uso: run archivo.elf\n");

    } else if (!paging_cpu_enabled()) {
        imprimir("ERROR: ejecuta pagingon.\n");

    } else {
        imprimir("Buscando archivo: ");
        imprimir(filename);
        imprimir("\n");

        if (elf_exec_file(filename) == 0) {
            imprimir("ELF ejecutado: OK\n");
            imprimir("Retorno a MiOS: OK\n");
        } else {
            imprimir("ERROR: no se pudo ejecutar ELF.\n");
        }
    }
}


else if (iguales(linea, "syswritestats")) {
    uint32_t accepted = syscall_write_accepted();
    uint32_t rejected = syscall_write_rejected();

    imprimir("=== ESTADISTICAS SYS_WRITE ===\n");

    imprimir("Solicitudes aceptadas: ");
    imprimir_hex32(accepted);
    imprimir("\n");

    imprimir("Solicitudes rechazadas: ");
    imprimir_hex32(rejected);
    imprimir("\n");
}

else if (iguales(linea, "binls")) {
    imprimir("=== PROGRAMAS BINFS ===\n");

    int count = binfs_count();

    if (count < 0) {
        imprimir("ERROR: catalogo BINFS invalido.\n");
    } else {
        int ok = 1;

        for (int i = 0; i < count; i++) {
            BinfsEntry entry;

            if (binfs_get_entry(
                    (uint32_t)i,
                    &entry
                ) != 0) {
                imprimir("ERROR: leyendo entrada.\n");
                ok = 0;
                break;
            }

            imprimir("Nombre: ");
            imprimir(entry.name);
            imprimir("\n");

            imprimir("Tamano (hex): ");
            imprimir_hex32(entry.size);
            imprimir("\n");

            imprimir("LBA inicial: ");
            imprimir_hex32(entry.lba);
            imprimir("\n");

            imprimir("------------------------\n");
        }

        if (ok) {
            imprimir("Total de archivos (hex): ");
            imprimir_hex32((uint32_t)count);
            imprimir("\n");

            imprimir("BINLS: OK\n");
        }
    }
}

else if (iguales(linea, "bincopy")) {
    /*
     * Sintaxis:
     * bincopy origen.elf destino.elf
     */

    char *source = args;

    while (*source == ' ')
        source++;

    char *destination = source;

    /* Buscar separacion entre nombres */
    while (*destination &&
           *destination != ' ') {
        destination++;
    }

    if (*destination != '\0') {
        *destination = '\0';
        destination++;

        while (*destination == ' ')
            destination++;
    }

    int valid = 1;

    if (*source == '\0' ||
        *destination == '\0') {
        valid = 0;
    }

    /* No permitir un tercer argumento */
    for (char *p = destination; *p; p++) {
        if (*p == ' ') {
            valid = 0;
            break;
        }
    }

    if (!valid) {
        imprimir("Uso: bincopy origen.elf destino.elf\n");

    } else {
        imprimir("Copiando: ");
        imprimir(source);
        imprimir(" -> ");
        imprimir(destination);
        imprimir("\n");

        if (binfs_copy_file(
                source,
                destination
            ) == 0) {

            imprimir("Archivo copiado: OK\n");
            imprimir("BIN COPY: OK\n");

        } else {
            imprimir("BIN COPY: ERROR\n");
        }
    }
}

else if (iguales(linea, "binrename")) {

    /* Sintaxis: binrename anterior nuevo */
    char *old_name = args;

    while (*old_name == ' ')
        old_name++;

    char *new_name = old_name;

    while (*new_name != '\0' &&
           *new_name != ' ') {
        new_name++;
    }

    if (*new_name == ' ') {
        *new_name = '\0';
        new_name++;

        while (*new_name == ' ')
            new_name++;
    }

    int valid = 1;

    if (*old_name == '\0' ||
        *new_name == '\0') {
        valid = 0;
    }

    /* Rechazar argumentos adicionales */
    for (char *p = new_name; *p; p++) {
        if (*p == ' ') {
            valid = 0;
            break;
        }
    }

    if (!valid) {
        imprimir("Uso: binrename anterior.elf nuevo.elf\n");

    } else {
        imprimir("Renombrando: ");
        imprimir(old_name);
        imprimir(" -> ");
        imprimir(new_name);
        imprimir("\n");

        if (binfs_rename_file(
                old_name,
                new_name
            ) == 0) {

            imprimir("Archivo renombrado: OK\n");
            imprimir("BIN RENAME: OK\n");

        } else {
            imprimir("BIN RENAME: ERROR\n");
        }
    }
}

else if (iguales(linea, "binrm")) {

    const char *filename = args;

    while (*filename == ' ') {
        filename++;
    }

    int valid = (*filename != '\0');

    /* Rechazar argumentos adicionales */
    for (const char *p = filename; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            valid = 0;
            break;
        }
    }

    if (!valid) {
        imprimir("Uso: binrm archivo.elf\n");

    } else {
        imprimir("Eliminando: ");
        imprimir(filename);
        imprimir("\n");

        if (binfs_remove_file(filename) == 0) {
            imprimir("Archivo retirado del catalogo: OK\n");
            imprimir("BIN RM: OK\n");
        }  else {
    imprimir("BIN RM: ERROR\n");
    imprimir("Archivo inexistente o catalogo invalido.\n");
}
    }
}

else if (iguales(linea, "binverify")) {
    imprimir("=== VERIFICACION BINFS ===\n");

    BinfsVerifyResult result;

    int status = binfs_verify_all(&result);

    imprimir("Archivos revisados: ");
    imprimir_hex32(result.files_checked);
    imprimir("\n");

    imprimir("ELF validos: ");
    imprimir_hex32(result.elf_valid);
    imprimir("\n");

    imprimir("Errores detectados: ");
    imprimir_hex32(result.errors);
    imprimir("\n");

    if (status == 0) {
        imprimir("Lectura BINFS: OK\n");
        imprimir("Checksums: OK\n");
        imprimir("Nombres y LBA: OK\n");
        imprimir("Segmentos ELF32: OK\n");
        imprimir("BIN VERIFY: OK\n");
    } else {
        imprimir("BIN VERIFY: ERROR\n");
    }
}

else if (iguales(linea, "binreplace")) {

    /* Sintaxis: binreplace origen destino */
    char *source = args;

    while (*source == ' ')
        source++;

    char *destination = source;

    while (*destination != '\0' &&
           *destination != ' ') {
        destination++;
    }

    if (*destination == ' ') {
        *destination = '\0';
        destination++;

        while (*destination == ' ')
            destination++;
    }

    int valid =
        (*source != '\0' &&
         *destination != '\0');

    for (char *p = destination; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            valid = 0;
            break;
        }
    }

    if (!valid) {
        imprimir("Uso: binreplace origen.elf destino.elf\n");

    } else {
        imprimir("Actualizando: ");
        imprimir(destination);
        imprimir("\n");

        if (binfs_replace_file(
                source,
                destination
            ) == 0) {

            imprimir("Datos actualizados: OK\n");
            imprimir("BIN REPLACE: OK\n");

        } else {
            imprimir("BIN REPLACE: ERROR\n");
        }
    }
}


else if (iguales(linea, "binmirrorcheck")) {

    imprimir("=== CATALOGO BINFS A/B ===\n");

    if (binfs_mirror_verify() == 0) {

        imprimir("Catalogo LBA 512: OK\n");
        imprimir("Catalogo LBA 600: OK\n");
        imprimir("Comparacion de 512 bytes: OK\n");
        imprimir("BIN MIRROR CHECK: OK\n");

    } else {

        imprimir("CATALOGO: DIFERENCIAS O ERROR\n");
        imprimir("BIN MIRROR CHECK: ERROR\n");
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
