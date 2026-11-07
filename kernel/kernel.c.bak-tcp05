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
#include "binfs_consistency.h"
#include "miosfs2_bitmap.h"
#include "miosfs2.h"
#include "miosfs2_disk.h"
#include "gui.h"
#include "framebuffer.h"
#include "graphics.h"
#include "serial.h"
#include "fb_draw.h"
#include "ps2_input.h"
#include "mouse.h"
#include "fb_text.h"
#include "net_udp.h"
#include "net_dns.h"
#include "net_tcp.h"
/*
 * 1 = demostracion grafica automatica.
 * 0 = arranque normal de MiOS.
 */
#define MIOS_FB_DEMO 1
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
    /* Sector temporal para prueba de publicacion */
static uint8_t binpublish_buffer[512];
    
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
static uint8_t recover_catalog_buffer[512];
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
    imprimir("bingencheck\n");
    imprimir("binpublishcheck\n");
    imprimir("binrepair\n");
    imprimir("binconsistency\n");
    imprimir("binrecoverycheck\n");
    imprimir("binrecoverselect\n");
    imprimir("fs2bitmaptest\n");
    imprimir("fs2test\n");
    imprimir("fs2diskcheck\n");
imprimir("fs2block0\n");
imprimir("fs2reserve0\n");
imprimir("fs2free0\n");
imprimir("gui\n");
imprimir("fbcheck\n");
imprimir("gfxtest\n");
imprimir("serialtest\n");
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

else if (iguales(linea, "bingencheck")) {
    imprimir("=== GENERACIONES BINFS ===\n");

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0) {
        imprimir("BIN GENERATION CHECK: ERROR\n");
    } else {
        imprimir("Catalogo A: ");
        imprimir(status.primary_valid ? "OK\n" : "ERROR\n");

        imprimir("Catalogo B: ");
        imprimir(status.mirror_valid ? "OK\n" : "ERROR\n");

        imprimir("Generacion A: ");
        imprimir_hex32(status.primary_generation);
        imprimir("\n");

        imprimir("Generacion B: ");
        imprimir_hex32(status.mirror_generation);
        imprimir("\n");

        imprimir("Catalogo seleccionado LBA: ");
        imprimir_hex32(status.selected_lba);
        imprimir("\n");

        imprimir("Generacion seleccionada: ");
        imprimir_hex32(status.selected_generation);
        imprimir("\n");

        imprimir("BIN GENERATION CHECK: OK\n");
    }
}

else if (iguales(linea, "binpublishcheck")) {
    imprimir("=== PUBLICACION CATALOGO A/B ===\n");

    uint32_t before_generation = 0;
    uint32_t before_lba = 0;

    int ok = 1;

    if (binfs_catalog_current(
            &before_generation,
            &before_lba
        ) != 0) {

        imprimir("ERROR: catalogo activo.\n");
        ok = 0;
    }

    /*
     * Leer los metadatos actuales.
     * No cambiaremos ninguna entrada.
     */
    if (ok &&
        binfs_catalog_read_latest(
            binpublish_buffer
        ) < 0) {

        imprimir("ERROR: leer catalogo.\n");
        ok = 0;
    }

    /* Publicar una nueva generacion */
    if (ok &&
        binfs_catalog_publish(
            binpublish_buffer,
            before_generation
        ) != 0) {

        imprimir("ERROR: publicacion.\n");
        ok = 0;
    }

    uint32_t after_generation = 0;
    uint32_t after_lba = 0;

    if (ok &&
        binfs_catalog_current(
            &after_generation,
            &after_lba
        ) != 0) {

        imprimir("ERROR: verificar publicacion.\n");
        ok = 0;
    }

    if (ok &&
        (after_generation !=
            before_generation + 1U ||
         after_lba == before_lba)) {

        imprimir("ERROR: generacion o LBA.\n");
        ok = 0;
    }

    if (ok) {
        imprimir("Generacion anterior: ");
        imprimir_hex32(before_generation);
        imprimir("\n");

        imprimir("Generacion nueva: ");
        imprimir_hex32(after_generation);
        imprimir("\n");

        imprimir("LBA anterior: ");
        imprimir_hex32(before_lba);
        imprimir("\n");

        imprimir("LBA nuevo: ");
        imprimir_hex32(after_lba);
        imprimir("\n");

        imprimir("Publicacion A/B: OK\n");
        imprimir("Checksum actualizado: OK\n");
        imprimir("BIN PUBLISH CHECK: OK\n");

    } else {
        imprimir("BIN PUBLISH CHECK: ERROR\n");
    }
}

else if (iguales(linea, "binrepair")) {
    imprimir("=== REPARACION BINFS A/B ===\n");

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0) {
        imprimir("ERROR: no existe catalogo valido.\n");

    } else if (status.primary_valid &&
               status.mirror_valid) {

        imprimir("Ambos catalogos son validos.\n");
        imprimir("No se requiere reparacion.\n");

    } else {
        /*
         * Antes de reparar metadatos,
         * verificar los archivos accesibles.
         */
        BinfsVerifyResult verification;

        if (binfs_verify_all(&verification) != 0) {
            imprimir("ERROR: archivos no integros.\n");
            imprimir("Reparacion cancelada.\n");

        } else {
            imprimir("Archivos verificados: OK\n");

            if (binfs_catalog_repair() != 0) {
                imprimir("BIN REPAIR: ERROR\n");

            } else if (binfs_mirror_verify() != 0) {
                imprimir("ERROR: catalogos diferentes.\n");

            } else {
                imprimir("Catalogo reconstruido: OK\n");
                imprimir("Checksum: OK\n");
                imprimir("Generaciones sincronizadas: OK\n");
                imprimir("BIN REPAIR: OK\n");
            }
        }
    }
}

else if (iguales(linea, "binconsistency")) {
    imprimir("=== COHERENCIA BINFS A/B ===\n");

    BinfsConsistencyReport report;

    if (binfs_consistency_check(&report) != 0) {
        imprimir("ERROR: lectura de catalogos.\n");
    } else {
        imprimir("Catalogo A: ");
        imprimir(
            report.a.catalog_valid
                ? "VALIDO\n" : "INVALIDO\n"
        );

        imprimir("Generacion A: ");
        imprimir_hex32(report.a.generation);
        imprimir("\n");

        imprimir("Archivos correctos A: ");
        imprimir_hex32(report.a.files_ok);
        imprimir("\n");

        imprimir("Archivos danados A: ");
        imprimir_hex32(report.a.files_damaged);
        imprimir("\n");

        imprimir("Catalogo B: ");
        imprimir(
            report.b.catalog_valid
                ? "VALIDO\n" : "INVALIDO\n"
        );

        imprimir("Generacion B: ");
        imprimir_hex32(report.b.generation);
        imprimir("\n");

        imprimir("Archivos correctos B: ");
        imprimir_hex32(report.b.files_ok);
        imprimir("\n");

        imprimir("Archivos danados B: ");
        imprimir_hex32(report.b.files_damaged);
        imprimir("\n");

        imprimir("Catalogo activo LBA: ");
        imprimir_hex32(report.selected_lba);
        imprimir("\n");

        if (report.selected_lba != 0U &&
            report.selected_data_ok) {

            imprimir("Datos activos integros: OK\n");
            imprimir("BIN CONSISTENCY: OK\n");

        } else {
            imprimir("Datos activos: NO VERIFICADOS\n");
            imprimir("BIN CONSISTENCY: ERROR\n");
        }
    }
}

else if (iguales(linea, "binrecoverycheck")) {
    imprimir("=== RECUPERACION BINFS A/B ===\n");

    BinfsRecoveryReport report;

    if (binfs_recovery_check(&report) != 0) {
        imprimir("BIN RECOVERY CHECK: ERROR\n");

    } else {
        imprimir("Catalogo activo LBA: ");
        imprimir_hex32(report.active_lba);
        imprimir("\n");

        imprimir("Generacion activa: ");
        imprimir_hex32(report.active_generation);
        imprimir("\n");

        imprimir("Datos activos: ");
        imprimir(
            report.active_data_ok
                ? "OK\n" : "ERROR\n"
        );

        imprimir("Datos alternativos: ");
        imprimir(
            report.fallback_data_ok
                ? "OK\n" : "NO RECUPERABLES\n"
        );

        imprimir("Versiones integras: ");
        imprimir_hex32(
            report.recoverable_catalogs
        );
        imprimir("\n");

        if (!report.active_data_ok) {
            imprimir(
                "ALERTA: version activa danada.\n"
            );
            imprimir(
                "No ejecutar actualizaciones.\n"
            );
            imprimir("BIN RECOVERY CHECK: ERROR\n");

        } else if (report.full_fallback_available) {
            imprimir("Version activa: OK\n");
            imprimir("Respaldo recuperable: OK\n");
            imprimir("BIN RECOVERY CHECK: OK\n");

        } else {
            imprimir("Version activa: OK\n");
            imprimir("Respaldo completo: NO\n");
            imprimir(
                "Se necesita nueva proteccion.\n"
            );
            imprimir("BIN RECOVERY CHECK: PARCIAL\n");
        }
    }
}

else if (iguales(linea, "binrecoverselect")) {
    imprimir("=== SELECCION RECUPERABLE ===\n");

    BinfsConsistencyReport report;

    if (binfs_consistency_check(&report) != 0) {
        imprimir("ERROR: diagnostico de datos.\n");
    } else {
        int slots =
            binfs_catalog_read_recoverable(
                recover_catalog_buffer
            );

        if (slots < 0) {
            imprimir("Sin version recuperable.\n");
            imprimir("BIN RECOVER SELECT: ERROR\n");
        } else {
            uint32_t generation =
                (uint32_t)recover_catalog_buffer[480] |
                ((uint32_t)recover_catalog_buffer[481] << 8) |
                ((uint32_t)recover_catalog_buffer[482] << 16) |
                ((uint32_t)recover_catalog_buffer[483] << 24);

            imprimir("Version recuperable: OK\n");

            imprimir("Generacion seleccionada: ");
            imprimir_hex32(generation);
            imprimir("\n");

            imprimir("Numero de slots: ");
            imprimir_hex32((uint32_t)slots);
            imprimir("\n");

            if (binfs_recovery_write_allowed()) {
                imprimir("Modificaciones: PERMITIDAS\n");
            } else {
                imprimir("Modificaciones: BLOQUEADAS\n");
            }

            imprimir("BIN RECOVER SELECT: OK\n");
        }
    }
}

else if (iguales(linea, "fs2test")) {
    imprimir("=== MIOSFS02 - FORMATO ===\n");

    if (miosfs2_selftest() == 0) {
        imprimir("Superbloque de 64 bytes: OK\n");
        imprimir("Nodo de 64 bytes: OK\n");
        imprimir("Checksum y estructura: OK\n");
        imprimir("Validacion de regiones: OK\n");
        imprimir("FS2 TEST: OK\n");
    } else {
        imprimir("FS2 TEST: ERROR\n");
    }
}

else if (iguales(linea, "fs2bitmaptest")) {
    imprimir("=== MIOSFS02 - BLOQUES ===\n");

    if (miosfs2_bitmap_selftest() == 0) {
        imprimir("Bitmap inicializado: OK\n");
        imprimir("Reserva de bloques: OK\n");
        imprimir("Bloques independientes: OK\n");
        imprimir("Liberacion: OK\n");
        imprimir("Doble liberacion rechazada: OK\n");
        imprimir("Reutilizacion: OK\n");
        imprimir("Limites de bloques: OK\n");
        imprimir("FS2 BITMAP TEST: OK\n");
    } else {
        imprimir("FS2 BITMAP TEST: ERROR\n");
    }
}

else if (iguales(linea, "fs2diskcheck")) {
    imprimir("=== MIOSFS02 - DISCO ===\n");

    if (miosfs2_disk_check() == 0) {
        imprimir("Superbloque: OK\n");
        imprimir("Distribucion: OK\n");
        imprimir("Bitmap en ATA: OK\n");
        imprimir("FS2 DISK CHECK: OK\n");
    } else {
        imprimir("FS2 DISK CHECK: ERROR\n");
        imprimir("Disco no compatible.\n");
    }
}
else if (iguales(linea, "fs2block0")) {
    int used = -1;

    if (miosfs2_disk_block0(&used) != 0) {
        imprimir("FS2 BLOCK0: ERROR\n");
    } else if (used) {
        imprimir("Bloque 0: OCUPADO\n");
    } else {
        imprimir("Bloque 0: LIBRE\n");
    }
}
else if (iguales(linea, "fs2reserve0")) {
    if (miosfs2_disk_set_block0(1) == 0) {
        imprimir("Bloque 0 reservado en disco.\n");
        imprimir("FS2 RESERVE0: OK\n");
    } else {
        imprimir("FS2 RESERVE0: ERROR\n");
    }
}
else if (iguales(linea, "fs2free0")) {
    if (miosfs2_disk_set_block0(0) == 0) {
        imprimir("Bloque 0 liberado en disco.\n");
        imprimir("FS2 FREE0: OK\n");
    } else {
        imprimir("FS2 FREE0: ERROR\n");
    }
}
else if (iguales(linea, "gui")) {
    gui_open();
}

else if (iguales(linea, "fbcheck")) {
    const MiosFramebufferInfo *fb =
        framebuffer_get_info();

    imprimir("=== MIOS FRAMEBUFFER ===\n");

    if (!fb->available) {
        imprimir("Framebuffer: NO DISPONIBLE\n");
        imprimir("Multiboot no entrego datos graficos.\n");
        imprimir("Modo VGA texto: conservado.\n");
    } else {
        imprimir("Framebuffer: DETECTADO\n");

        imprimir("Direccion baja: ");
        imprimir_hex32(fb->address_low);
        imprimir("\n");

        imprimir("Direccion alta: ");
        imprimir_hex32(fb->address_high);
        imprimir("\n");

        imprimir("Ancho: ");
        imprimir_hex32(fb->width);
        imprimir("\n");

        imprimir("Alto: ");
        imprimir_hex32(fb->height);
        imprimir("\n");

        imprimir("Pitch: ");
        imprimir_hex32(fb->pitch);
        imprimir("\n");

        imprimir("Bits por pixel: ");
        imprimir_hex32(fb->bpp);
        imprimir("\n");

        imprimir("Tipo: ");
        imprimir_hex32(fb->type);
        imprimir("\n");

      
if (fb->type == 2U) {
    imprimir("Modo de video: EGA/VGA TEXTO\n");
    imprimir("No admite dibujo RGB por pixel.\n");
    imprimir("FB CHECK: TEXT MODE\n");

} else if (fb->type == 1U &&
           fb->bpp == 32U &&
           fb->address_high == 0U) {

    imprimir("Modo de video: RGB 32 BPP\n");
    imprimir("Framebuffer grafico detectado.\n");
    imprimir("Pendiente: validar formato y mapear.\n");
    imprimir("FB CHECK: GRAPHICS DETECTED\n");

} else {
    imprimir("Formato de video no soportado.\n");
    imprimir("FB CHECK: UNSUPPORTED\n");
}

    }
}

else if (iguales(linea, "gfxtest")) {
    imprimir("=== MIOS GRAPHICS TEST ===\n");

    if (gfx_selftest() == 0) {
        imprimir("Buffer de pixeles: OK\n");
        imprimir("Color RGB: OK\n");
        imprimir("Dibujo de rectangulos: OK\n");
        imprimir("Limites de pantalla: OK\n");
        imprimir("GFX TEST: OK\n");
    } else {
        imprimir("GFX TEST: ERROR\n");
    }
}

else if (iguales(linea, "serialtest")) {
    imprimir("=== PRUEBA SERIAL COM1 ===\n");

    if (!serial_available()) {
        imprimir("SERIAL TEST: ERROR\n");
    } else {
        serial_write("\n");
        serial_write("MiOS COM1: FUNCIONANDO\n");
        serial_write("Kernel x86 32 bits\n");
        serial_write("SERIAL TEST: OK\n");

        imprimir("Mensaje enviado a COM1.\n");
        imprimir("SERIAL TEST: OK\n");
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

/*
 * MiOS Desktop 0.5C
 *
 * Determinar si el raton ha pulsado
 * una opcion del menu lateral.
 *
 * Devuelve:
 *  0..4 = ventana seleccionada
 *  -1   = fuera de los botones
 */
static int desktop_mouse_hit(
    int32_t x,
    int32_t y
) {
    const int32_t menu_x = 30;
    const int32_t menu_width = 112;

    const int32_t first_y = 136;
    const int32_t step = 54;
    const int32_t button_height = 43;

    if (x < menu_x ||
        x >= menu_x + menu_width) {
        return -1;
    }

    for (int i = 0; i < 5; i++) {
        int32_t top = first_y + i * step;

        if (y >= top &&
            y < top + button_height) {
            return i;
        }
    }

    return -1;
}

/*
 * MiOS Desktop 0.6
 * Accesos directos del escritorio
 */

#define MIOS_APP_NONE      (-1)
#define MIOS_APP_TERMINAL   5
#define MIOS_APP_FILES      3

/* ===== MiOS Desktop 0.6 - Terminal interactiva ===== */

#define TERM_LINES 9
#define TERM_COLS  54

static char term_history[TERM_LINES][TERM_COLS];
static int term_used = 0;

static char term_input[48];
static int term_len = 0;

/* Teclado PS/2: scancodes Set 1 */
static const char term_keys[0x3A] = {
    [0x02]='1', [0x03]='2', [0x04]='3',
    [0x05]='4', [0x06]='5', [0x07]='6',
    [0x08]='7', [0x09]='8', [0x0A]='9',
    [0x0B]='0', [0x0C]='-', [0x0D]='=',

    [0x10]='Q', [0x11]='W', [0x12]='E',
    [0x13]='R', [0x14]='T', [0x15]='Y',
    [0x16]='U', [0x17]='I', [0x18]='O',
    [0x19]='P',

    [0x1E]='A', [0x1F]='S', [0x20]='D',
    [0x21]='F', [0x22]='G', [0x23]='H',
    [0x24]='J', [0x25]='K', [0x26]='L',

    [0x2C]='Z', [0x2D]='X', [0x2E]='C',
    [0x2F]='V', [0x30]='B', [0x31]='N',
    [0x32]='M',

    [0x33]=',', [0x34]='.',
    [0x35]='/', [0x39]=' '
};

static int term_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }

    return *a == *b;
}

static void term_print(const char *message)
{
    int row;

    if (term_used < TERM_LINES) {
        row = term_used++;
    } else {
        for (int i = 1; i < TERM_LINES; i++) {
            for (int j = 0; j < TERM_COLS; j++)
                term_history[i - 1][j] = term_history[i][j];
        }
        row = TERM_LINES - 1;
    }

    int j = 0;

    while (message[j] && j < TERM_COLS - 1) {
        term_history[row][j] = message[j];
        j++;
    }

    term_history[row][j] = '\0';
}

static void term_print_number(const char *label, uint32_t number)
{
    char result[TERM_COLS];
    char digits[11];
    int p = 0;
    int n = 0;

    while (*label && p < TERM_COLS - 12)
        result[p++] = *label++;

    do {
        digits[n++] = '0' + (number % 10U);
        number /= 10U;
    } while (number != 0 && n < 11);

    while (n > 0)
        result[p++] = digits[--n];

    result[p] = '\0';
    term_print(result);
}

/* ===== Terminal: acceso real a BINFS ===== */

static uint8_t term_file_buffer[BINFS_MAX_FILE_SIZE];

static void term_get_filename(
    const BinfsEntry *entry,
    char name[33])
{
    for (int i = 0; i < 32; i++)
        name[i] = entry->name[i];

    name[32] = '\0';
}

/* Comparar nombres sin distinguir mayusculas */
static int term_filename_equals(
    const char *a,
    const char *b)
{
    for (int i = 0; i < 32; i++) {
        char x = a[i];
        char y = b[i];

        if (x >= 'a' && x <= 'z')
            x -= 32;

        if (y >= 'a' && y <= 'z')
            y -= 32;

        if (x != y)
            return 0;

        if (x == '\0')
            return 1;
    }

    return a[32] == '\0';
}

/* Comando LS */
static void term_command_ls(void)
{
    int count = binfs_count();

    if (count < 0) {
        term_print("BINFS CATALOG ERROR");
        return;
    }

    if (count == 0) {
        term_print("NO FILES FOUND");
        return;
    }

    for (int i = 0; i < count; i++) {
        BinfsEntry entry;

        if (binfs_get_entry((uint32_t)i, &entry) == 0) {
            char name[33];
            term_get_filename(&entry, name);
            term_print(name);
        }
    }
}

/* Comando CAT */
static void term_command_cat(const char *filename)
{
    int count = binfs_count();

    if (count < 0) {
        term_print("BINFS CATALOG ERROR");
        return;
    }

    for (int n = 0; n < count; n++) {
        BinfsEntry entry;

        if (binfs_get_entry((uint32_t)n, &entry) != 0)
            continue;

        char name[33];
        term_get_filename(&entry, name);

        if (!term_filename_equals(filename, name))
            continue;

        uint32_t bytes_read = 0;

        if (binfs_read(
                name,
                term_file_buffer,
                BINFS_MAX_FILE_SIZE,
                &bytes_read) != 0 ||
            bytes_read > BINFS_MAX_FILE_SIZE)
        {
            term_print("FILE READ ERROR");
            return;
        }

        if (bytes_read == 0) {
            term_print("EMPTY FILE");
            return;
        }

        char line[TERM_COLS];
        int pos = 0;
        int lines = 0;
        uint32_t i = 0;

        /* Mostrar hasta 7 lineas de texto */
        while (i < bytes_read && lines < 7) {
            uint8_t ch = term_file_buffer[i++];

            if (ch == '\r')
                continue;

            if (ch == '\n') {
                line[pos] = '\0';
                term_print(line);
                pos = 0;
                lines++;
                continue;
            }

            if (ch == '\t')
                ch = ' ';

            if (ch < 32 || ch > 126)
                ch = '?';

            if (pos == TERM_COLS - 1) {
                line[pos] = '\0';
                term_print(line);
                pos = 0;
                lines++;
            }

            if (lines < 7)
                line[pos++] = (char)ch;
        }

        if (pos > 0 && lines < 7) {
            line[pos] = '\0';
            term_print(line);
        }

        if (i < bytes_read)
            term_print("[OUTPUT TRUNCATED]");

        return;
    }

    term_print("FILE NOT FOUND");
}


static void term_execute(void)
{
    if (term_len == 0)
        return;

    if (term_equals(term_input, "CLEAR")) {
        term_used = 0;
        return;
    }

    term_print(term_input);

    if (term_equals(term_input, "HELP")) {
        term_print("HELP CLEAR ECHO MEM VER FILES");
    }
    else if (term_equals(term_input, "VER")) {
        term_print("MiOS Desktop 0.6");
        term_print("KERNEL X86 32 BITS");
    }
    else if (term_equals(term_input, "MEM")) {
        term_print_number("TOTAL FRAMES: ",
                          pmm_total_frames());

        term_print_number("FREE FRAMES: ",
                          pmm_free_frames());
    }
    else if (term_equals(term_input, "FILES")) {
        term_print_number("BINFS ENTRIES: ",
                          (uint32_t)binfs_count());
    }
    
else if (term_equals(term_input, "LS")) {
    term_command_ls();
}
else if (term_equals(term_input, "CAT")) {
    term_print("USAGE: CAT FILENAME");
}
else if (term_input[0] == 'C' &&
         term_input[1] == 'A' &&
         term_input[2] == 'T' &&
         term_input[3] == ' ') {
    term_command_cat(term_input + 4);
}

    else if (term_input[0] == 'E' &&
             term_input[1] == 'C' &&
             term_input[2] == 'H' &&
             term_input[3] == 'O' &&
             term_input[4] == ' ') {
        term_print(term_input + 5);
    }
    else {
        term_print("UNKNOWN COMMAND - TYPE HELP");
    }
}

/* Devuelve 1 si hay que actualizar la ventana */
static int term_handle_key(uint8_t code)
{
    /* Ignorar liberaciones de teclas */
    if (code & 0x80U)
        return 0;

    /* ENTER */
    if (code == 0x1C) {
        term_execute();
        term_len = 0;
        term_input[0] = '\0';
        return 1;
    }

    /* BACKSPACE */
    if (code == 0x0E) {
        if (term_len > 0) {
            term_len--;
            term_input[term_len] = '\0';
            return 1;
        }
        return 0;
    }

    if (code >= sizeof(term_keys))
        return 0;

    char ch = term_keys[code];

    if (!ch || term_len >= 46)
        return 0;

    term_input[term_len++] = ch;
    term_input[term_len] = '\0';

    return 1;
}

static void term_draw_content(void)
{
    /* Redibujar SOLO el interior de la terminal */
    fb_draw_rect(197, 157, 389, 237, 16, 23, 35);

    fb_text_draw(
        207, 169, "MiOS INTERACTIVE TERMINAL", 1,
        100, 230, 170
    );

    for (int i = 0; i < term_used; i++) {
        fb_text_draw(
            207, 193 + i * 16,
            term_history[i], 1,
            225, 235, 240
        );
    }

    char prompt[55] = "MIOS> ";
    int p = 6;

    for (int i = 0; i < term_len; i++)
        prompt[p++] = term_input[i];

    prompt[p++] = '_';
    prompt[p] = '\0';

    fb_text_draw(
        207, 356, prompt, 1,
        135, 255, 165
    );
}

static void desktop_draw_app_icons(void)
{
    /* Icono de terminal */
    fb_draw_rect(200, 143, 74, 76, 38, 77, 113);
    fb_draw_rect(216, 151, 42, 34, 18, 29, 45);

    /* Simbolo >_ */
    fb_text_draw(
        224, 163, ">_", 1,
        135, 255, 164
    );

    fb_text_draw(
        207, 198, "TERMINAL", 1,
        255, 255, 255
    );

    /* Icono de carpeta */
    fb_draw_rect(315, 143, 74, 76, 38, 77, 113);

    /* Pestana y cuerpo de carpeta */
    fb_draw_rect(327, 154, 24, 7, 246, 188, 77);
    fb_draw_rect(327, 160, 50, 32, 243, 193, 76);
    fb_draw_rect(330, 166, 44, 23, 255, 215, 118);

    fb_text_draw(
        326, 198, "ARCHIVOS", 1,
        255, 255, 255
    );
}

/*
 * Detectar clic sobre iconos.
 */
static int desktop_app_hit(int x, int y)
{
    if (x >= 200 && x < 274 &&
        y >= 143 && y < 219)
        return MIOS_APP_TERMINAL;

    if (x >= 315 && x < 389 &&
        y >= 143 && y < 219)
        return MIOS_APP_FILES;

    return MIOS_APP_NONE;
}
/*
 * MiOS Desktop 0.6
 * Declaraciones anticipadas.
 */
static void desktop_draw_app_icons(void);
static int desktop_app_hit(int x, int y);
static void desktop_draw_terminal(void);
static int desktop_render_app(uint32_t window);

/* MiOS Network 0.1 - Deteccion PCI RTL8139 */

static uint32_t net_pci_read(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t reg)
{
    uint32_t address =
        0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (reg & 0xFCU);

    uint32_t value;

    __asm__ volatile(
        "outl %0, %w1"
        :
        : "a"(address), "Nd"((uint16_t)0xCF8)
    );

    __asm__ volatile(
        "inl %w1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)0xCFC)
    );

    return value;
}

static void mios_network_detect(void)
{
    serial_write("NET: Buscando RTL8139...\n");

    /* Buscar en el bus PCI 0 de QEMU */
    for (uint32_t slot = 0; slot < 32; slot++) {
        for (uint32_t func = 0; func < 8; func++) {

            uint32_t id = net_pci_read(
                0,
                (uint8_t)slot,
                (uint8_t)func,
                0
            );

            uint16_t vendor = (uint16_t)id;
            uint16_t device = (uint16_t)(id >> 16);

            if (vendor == 0x10EC &&
                device == 0x8139) {

                serial_write(
                    "NET: RTL8139 ENCONTRADA!\n"
                );

                serial_write("NET: PCI BAR0 = ");
                serial_write_hex32(
                    net_pci_read(
                        0,
                        (uint8_t)slot,
                        (uint8_t)func,
                        0x10
                    )
                );
                serial_write("\n");

                return;
            }
        }
    }

    serial_write("NET: RTL8139 NO ENCONTRADA\n");
}

/* ===== MiOS Network 0.2 - RTL8139 ===== */

static uint16_t rtl8139_io_base = 0;
/* RTL8139 - Buffers para DMA */

/* Recepcion: 8 KB + margen requerido */
static uint8_t rtl_rx_buffer[12288]
    __attribute__((aligned(4096)));

/* Transmision: una trama Ethernet */
static uint8_t rtl_tx_buffer[2048]
    __attribute__((aligned(4096)));
static uint8_t rtl8139_in8(uint16_t port)
{
    uint8_t value;

    __asm__ volatile(
        "inb %w1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void rtl8139_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile(
        "outb %0, %w1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void rtl8139_out16(uint16_t port, uint16_t value)
{
    __asm__ volatile(
        "outw %w0, %w1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void rtl8139_pci_enable(
    uint8_t slot,
    uint8_t func,
    uint16_t command)
{
    uint32_t address =
        0x80000000U |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        0x04U;

    __asm__ volatile(
        "outl %0, %w1"
        :
        : "a"(address), "Nd"((uint16_t)0xCF8)
    );

    __asm__ volatile(
        "outw %w0, %w1"
        :
        : "a"(command), "Nd"((uint16_t)0xCFC)
    );
}

static int rtl8139_init_basic(void)
{
    serial_write("RTL8139: Inicializando...\n");

    for (uint32_t slot = 0; slot < 32; slot++) {
        for (uint32_t func = 0; func < 8; func++) {

            uint32_t id = net_pci_read(
                0, (uint8_t)slot,
                (uint8_t)func, 0
            );

            if ((uint16_t)id != 0x10EC ||
                (uint16_t)(id >> 16) != 0x8139)
                continue;

            uint32_t bar = net_pci_read(
                0, (uint8_t)slot,
                (uint8_t)func, 0x10
            );

            if ((bar & 1U) == 0) {
                serial_write("RTL8139: BAR0 no es IO\n");
                return -1;
            }

            rtl8139_io_base =
                (uint16_t)(bar & ~3U);

            if (rtl8139_io_base == 0)
                return -1;

            serial_write("RTL8139: IO BASE = ");
            serial_write_hex32(rtl8139_io_base);
            serial_write("\n");

            /* Activar IO y PCI Bus Master */
            uint32_t pci_command = net_pci_read(
                0, (uint8_t)slot,
                (uint8_t)func, 0x04
            );

            rtl8139_pci_enable(
                (uint8_t)slot,
                (uint8_t)func,
                (uint16_t)(pci_command | 0x0005U)
            );

            /* Encender y reiniciar la tarjeta */
            rtl8139_out8(rtl8139_io_base + 0x52, 0);
            rtl8139_out8(rtl8139_io_base + 0x37, 0x10);

            uint32_t timeout = 1000000;

            while (timeout-- > 0) {
                if (!(rtl8139_in8(
                        rtl8139_io_base + 0x37) & 0x10))
                    break;
            }

            if (rtl8139_in8(
                    rtl8139_io_base + 0x37) & 0x10) {
                serial_write("RTL8139: RESET ERROR\n");
                return -1;
            }

            /* Sin interrupciones: usaremos polling */
            rtl8139_out16(
                rtl8139_io_base + 0x3C, 0
            );

            serial_write("RTL8139: RESET OK\n");

            /* Leer direccion MAC */
            serial_write("RTL8139: MAC BYTES:\n");

            for (int i = 0; i < 6; i++) {
                serial_write_hex32(
                    rtl8139_in8(
                        rtl8139_io_base + (uint16_t)i
                    )
                );
                serial_write("\n");
            }

            serial_write("RTL8139: HARDWARE LISTO\n");
            return 0;
        }
    }

    serial_write("RTL8139: NO ENCONTRADA\n");
    return -1;
}

/* ===== MiOS Network 0.2C: DMA + ARP ===== */

static void rtl8139_out32(uint16_t port, uint32_t value)
{
    __asm__ volatile(
        "outl %0, %w1"
        :
        : "a"(value), "Nd"(port)
    );
}

static uint32_t rtl8139_in32(uint16_t port)
{
    uint32_t value;

    __asm__ volatile(
        "inl %w1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/* ===== MiOS Network 0.3: IPv4 + ICMP ===== */

static uint16_t net_checksum(
    const uint8_t *data,
    uint32_t length)
{
    uint32_t sum = 0;

    for (uint32_t i = 0; i + 1U < length; i += 2U) {
        sum += ((uint32_t)data[i] << 8) |
               data[i + 1U];
    }

    if (length & 1U)
        sum += (uint32_t)data[length - 1U] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);

    return (uint16_t)~sum;
}

static int rtl8139_ping_gateway(
    const uint8_t gateway_mac[6],
    uint32_t *rx_offset)
{
    uint16_t io = rtl8139_io_base;

    if (io == 0 || rx_offset == 0 || gateway_mac == 0)
        return -1;

    /* Limpiar el paquete de transmision */
    for (uint32_t i = 0; i < 60U; i++)
        rtl_tx_buffer[i] = 0;

    /* Cabecera Ethernet */
    for (uint32_t i = 0; i < 6U; i++) {
        rtl_tx_buffer[i] = gateway_mac[i];
        rtl_tx_buffer[6U + i] =
            rtl8139_in8(io + (uint16_t)i);
    }

    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x00;  /* IPv4 */

    /* Cabecera IPv4 */
    uint8_t *ip = &rtl_tx_buffer[14];

    ip[0] = 0x45;              /* IPv4, 20 bytes */
    ip[2] = 0;
    ip[3] = 44;                /* Tamano IPv4 total */
    ip[4] = 0x12;
    ip[5] = 0x34;
    ip[8] = 64;                /* TTL */
    ip[9] = 1;                 /* ICMP */

    /* IP origen: 10.0.2.15 */
    ip[12] = 10;
    ip[13] = 0;
    ip[14] = 2;
    ip[15] = 15;

    /* IP destino: 10.0.2.2 */
    ip[16] = 10;
    ip[17] = 0;
    ip[18] = 2;
    ip[19] = 2;

    uint16_t checksum = net_checksum(ip, 20);

    ip[10] = (uint8_t)(checksum >> 8);
    ip[11] = (uint8_t)checksum;

    /* ICMP Echo Request */
    uint8_t *icmp = ip + 20;

    icmp[0] = 8;               /* Echo Request */
    icmp[4] = 0x4D;
    icmp[5] = 0x49;            /* Identificador MI */
    icmp[6] = 0;
    icmp[7] = 1;               /* Secuencia 1 */

    for (uint32_t i = 0; i < 16U; i++)
        icmp[8U + i] = (uint8_t)('A' + i);

    checksum = net_checksum(icmp, 24);

    icmp[2] = (uint8_t)(checksum >> 8);
    icmp[3] = (uint8_t)checksum;

  
/* Enviar ping mediante TX1 */
__asm__ volatile("" ::: "memory");

rtl8139_out32(
    io + 0x24,
    (uint32_t)(uintptr_t)rtl_tx_buffer
);

rtl8139_out32(
    io + 0x14,
    60U
);

int sent = 0;

for (uint32_t n = 0; n < 5000000U; n++) {
    uint32_t tx = rtl8139_in32(io + 0x14);

    if (tx & 0x00008000U) {
        sent = 1;
        break;
    }
}


if (!sent) {
    serial_write("NET: PING TX ERROR\n");

    serial_write("NET: TSD0 = ");
    serial_write_hex32(
        rtl8139_in32(io + 0x10)
    );
    serial_write("\n");

    serial_write("NET: TSD1 = ");
    serial_write_hex32(
        rtl8139_in32(io + 0x14)
    );
    serial_write("\n");

    serial_write("NET: TSD2 = ");
    serial_write_hex32(
        rtl8139_in32(io + 0x18)
    );
    serial_write("\n");

    serial_write("NET: TSD3 = ");
    serial_write_hex32(
        rtl8139_in32(io + 0x1C)
    );
    serial_write("\n");

    return -1;
}



    serial_write("NET: PING 10.0.2.2 ENVIADO\n");

    /* Esperar respuesta usando el mismo anillo RX */
    for (uint32_t n = 0; n < 10000000U; n++) {

        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        uint32_t offset = *rx_offset;

        volatile uint8_t *packet =
            &rtl_rx_buffer[offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 4U ||
            length > 2048U ||
            offset + 4U + length > sizeof(rtl_rx_buffer))
        {
            serial_write("NET: PING RX ERROR\n");
            return -1;
        }

        volatile uint8_t *frame = packet + 4;
        int is_reply = 0;

        /* Comprobar respuesta IPv4/ICMP */
        if (length >= 46U &&
            frame[12] == 8 &&
            frame[13] == 0 &&
            (frame[14] >> 4) == 4 &&
            frame[23] == 1 &&
            frame[26] == 10 &&
            frame[27] == 0 &&
            frame[28] == 2 &&
            frame[29] == 2 &&
            frame[30] == 10 &&
            frame[31] == 0 &&
            frame[32] == 2 &&
            frame[33] == 15)
        {
            uint32_t ihl =
                (uint32_t)(frame[14] & 15U) * 4U;

            uint32_t p = 14U + ihl;

            if (ihl >= 20U &&
                ihl <= 60U &&
                length >= p + 8U + 4U &&
                frame[p] == 0 &&
                frame[p + 1U] == 0 &&
                frame[p + 4U] == 0x4D &&
                frame[p + 5U] == 0x49 &&
                frame[p + 6U] == 0 &&
                frame[p + 7U] == 1)
            {
                is_reply = 1;
            }
        }

        /* Liberar paquete del anillo */
        offset =
            (offset + 4U + length + 3U) & ~3U;

        offset &= 8191U;
        *rx_offset = offset;

        rtl8139_out16(
            io + 0x38,
            (uint16_t)(offset - 16U)
        );

        rtl8139_out16(io + 0x3E, 1U);

        if (is_reply) {
            serial_write("NET: PING 10.0.2.2 OK!\n");
            return 0;
        }
    }

    serial_write("NET: PING TIMEOUT\n");
    return -1;
}

/*
 * MiOS Network 0.4A.2
 *
 * Enviar un paquete UDP por RTL8139
 * hacia la puerta de enlace QEMU.
 *
 * Requiere:
 * - RTL8139 inicializada
 * - ARP resuelto
 * - TX DMA disponible
 */

/*
 * MiOS DNS 0.2
 * Prueba integrada DNS + UDP + IPv4.
 * Todavia no transmite por Ethernet.
 */
static int mios_dns_packet_test(void)
{
    uint8_t dns_query[128];
    uint8_t ip_packet[256];

    const uint8_t src_ip[4] = {
        10, 0, 2, 15
    };

    const uint8_t dns_ip[4] = {
        10, 0, 2, 3
    };

    int dns_size = net_dns_build_query(
        dns_query,
        sizeof(dns_query),
        "example.com",
        0x4D49
    );

    if (dns_size != 29) {
        serial_write(
            "NET: DNS QUERY BUILD ERROR\n"
        );
        return -1;
    }

    serial_write(
        "NET: DNS QUERY example.com OK\n"
    );

    int ip_size = net_udp_build_ipv4(
        ip_packet,
        sizeof(ip_packet),
        src_ip,
        dns_ip,
        49153,
        53,
        dns_query,
        (uint16_t)dns_size
    );

    if (ip_size != 57) {
        serial_write(
            "NET: DNS UDP BUILD ERROR\n"
        );
        return -1;
    }

    /* IPv4 + protocolo UDP */
    if (ip_packet[0] != 0x45 ||
        ip_packet[9] != 17) {
        return -1;
    }

    /* Puerto destino DNS: 53 */
    if (ip_packet[22] != 0 ||
        ip_packet[23] != 53) {
        return -1;
    }

    /* IP destino: 10.0.2.3 */
    if (ip_packet[16] != 10 ||
        ip_packet[17] != 0 ||
        ip_packet[18] != 2 ||
        ip_packet[19] != 3) {
        return -1;
    }

    serial_write(
        "NET: DNS UDP PORT 53 OK\n"
    );
    serial_write(
        "NET: DNS DESTINO 10.0.2.3 OK\n"
    );
    serial_write(
        "NET: DNS IPV4 PACKET OK\n"
    );

    return 0;
}

static int rtl8139_udp_send_gateway(
    const uint8_t gateway_mac[6]
) {
    if (gateway_mac == 0 ||
        rtl8139_io_base == 0) {
        return -1;
    }

    uint16_t io = rtl8139_io_base;

    const uint8_t src_ip[4] = {
        10, 0, 2, 15
    };

    const uint8_t dst_ip[4] = {
        10, 0, 2, 2
    };

    const uint8_t message[] = {
        'H', 'O', 'L', 'A', ' ',
        'U', 'D', 'P', ' ',
        'M', 'I', 'O', 'S'
    };

    /*
     * Preparar un unico buffer DMA.
     * La prueba ARP/ICMP ya termino.
     */
    for (uint32_t i = 0;
         i < sizeof(rtl_tx_buffer);
         i++) {
        rtl_tx_buffer[i] = 0;
    }

    /* MAC destino: puerta de enlace */
    for (uint32_t i = 0; i < 6U; i++) {
        rtl_tx_buffer[i] = gateway_mac[i];

        /* MAC origen: nuestra RTL8139 */
        rtl_tx_buffer[6U + i] =
            rtl8139_in8(io + (uint16_t)i);
    }

    /* EtherType IPv4 */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x00;

    /*
     * Construir IPv4 + UDP despues
     * de los 14 bytes Ethernet.
     */
    int ip_size = net_udp_build_ipv4(
        &rtl_tx_buffer[14],
        sizeof(rtl_tx_buffer) - 14U,
        src_ip,
        dst_ip,
        49152U,
        5555U,
        message,
        (uint16_t)sizeof(message)
    );

    if (ip_size < 0) {
        serial_write(
            "NET: UDP BUILD ERROR\n"
        );
        return -1;
    }

    uint32_t frame_size =
        14U + (uint32_t)ip_size;

    /* Ethernet exige al menos 60 bytes sin FCS */
    if (frame_size < 60U)
        frame_size = 60U;

    if (frame_size > sizeof(rtl_tx_buffer))
        return -1;

    /*
     * Usar descriptor TX2:
     * TSAD2 = IO + 0x28
     * TSD2  = IO + 0x18
     */
    __asm__ volatile("" ::: "memory");

    rtl8139_out32(
        io + 0x28,
        (uint32_t)(uintptr_t)rtl_tx_buffer
    );

    rtl8139_out32(
        io + 0x18,
        frame_size
    );

    /* Esperar confirmacion de transmision */
    for (uint32_t n = 0;
         n < 5000000U;
         n++) {

        uint32_t status =
            rtl8139_in32(io + 0x18);

        /* TOK: transmision completada */
        if (status & 0x00008000U) {
            serial_write(
                "NET: UDP TX OK\n"
            );
            serial_write(
                "NET: DESTINO 10.0.2.2:5555\n"
            );
            serial_write(
                "NET: MENSAJE HOLA UDP MIOS\n"
            );
            return 0;
        }

        /* TABT: transmision abortada */
        if (status & 0x40000000U) {
            serial_write(
                "NET: UDP TX ABORTADA\n"
            );
            return -1;
        }
    }

    serial_write("NET: UDP TX TIMEOUT\n");
    return -1;
}

/*
 * MiOS Network 0.4A.3
 * Recibir respuesta UDP desde QEMU/Ubuntu.
 *
 * Reutiliza el anillo RX de ARP e ICMP.
 */
static int rtl8139_udp_receive(
    uint32_t *rx_offset
) {
    if (rtl8139_io_base == 0 ||
        rx_offset == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    serial_write(
        "NET: ESPERANDO RESPUESTA UDP...\n"
    );

    for (uint32_t n = 0;
         n < 100000000U;
         n++) {

        /* BUFE=1: anillo RX vacio */
        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        uint32_t offset = *rx_offset;

        volatile uint8_t *packet =
            &rtl_rx_buffer[offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 4U ||
            length > 2048U ||
            offset + 4U + length >
                sizeof(rtl_rx_buffer)) {

            serial_write(
                "NET: UDP RX PACKET ERROR\n"
            );
            return -1;
        }

        volatile uint8_t *frame = packet + 4;

        int valid_reply = 0;

        /*
         * RX length incluye los 4 bytes
         * del FCS Ethernet.
         */
        uint32_t frame_size =
            (uint32_t)length - 4U;

        if (frame_size >= 14U + 20U + 8U &&
            frame[12] == 0x08 &&
            frame[13] == 0x00) {

            volatile uint8_t *ip =
                frame + 14;

            uint32_t ihl =
                (uint32_t)(ip[0] & 15U) * 4U;

            uint32_t ip_total =
                ((uint32_t)ip[2] << 8) |
                (uint32_t)ip[3];

            uint16_t frag =
                ((uint16_t)ip[6] << 8) |
                (uint16_t)ip[7];

            if ((ip[0] >> 4) == 4U &&
                ihl >= 20U &&
                ihl <= 60U &&
                frame_size >= 14U + ihl + 8U &&
                ip_total >= ihl + 8U &&
                ip_total <= frame_size - 14U &&
                ip[9] == 17U &&
                (frag & 0x3FFFU) == 0U &&

                /* IP origen: 10.0.2.2 */
                ip[12] == 10 &&
                ip[13] == 0 &&
                ip[14] == 2 &&
                ip[15] == 2 &&

                /* IP destino: 10.0.2.15 */
                ip[16] == 10 &&
                ip[17] == 0 &&
                ip[18] == 2 &&
                ip[19] == 15) {

                volatile uint8_t *udp =
                    ip + ihl;

                uint16_t src_port =
                    ((uint16_t)udp[0] << 8) |
                    udp[1];

                uint16_t dst_port =
                    ((uint16_t)udp[2] << 8) |
                    udp[3];

                uint16_t udp_length =
                    ((uint16_t)udp[4] << 8) |
                    udp[5];

                static const char expected[] =
                    "MIOS UDP OK";

                const uint32_t expected_len =
                    sizeof(expected) - 1U;

                if (src_port == 5555U &&
                    dst_port == 49152U &&
                    udp_length ==
                        8U + expected_len &&
                    udp_length <= ip_total - ihl) {

                    volatile uint8_t *data =
                        udp + 8;

                    valid_reply = 1;

                    for (uint32_t i = 0;
                         i < expected_len;
                         i++) {

                        if (data[i] !=
                            (uint8_t)expected[i]) {

                            valid_reply = 0;
                            break;
                        }
                    }
                }
            }
        }

        /*
         * Liberar el paquete y avanzar
         * en el mismo anillo RX.
         */
        offset =
            (offset + 4U + length + 3U)
            & ~3U;

        offset &= 8191U;

        *rx_offset = offset;

        rtl8139_out16(
            io + 0x38,
            (uint16_t)(offset - 16U)
        );

        rtl8139_out16(
            io + 0x3E,
            0x0001U
        );

        if (valid_reply) {
            serial_write(
                "NET: UDP RESPUESTA RECIBIDA!\n"
            );

            serial_write(
                "NET: ORIGEN 10.0.2.2:5555\n"
            );

            serial_write(
                "NET: MENSAJE MIOS UDP OK\n"
            );

            serial_write(
                "NET: UDP RX OK\n"
            );

            return 0;
        }
    }

    serial_write(
        "NET: UDP RX TIMEOUT\n"
    );

    return -1;
}

/*
 * MiOS DNS - resolver MAC de 10.0.2.3
 *
 * Reutiliza el anillo RX existente.
 * Se llama despues de finalizar UDP.
 */
static int rtl8139_dns_arp(
    uint8_t dns_mac[6],
    uint32_t *rx_offset
) {
    if (!dns_mac || !rx_offset ||
        rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    /* Preparar ARP Request Ethernet */
    for (uint32_t i = 0; i < 60U; i++)
        rtl_tx_buffer[i] = 0;

    for (uint32_t i = 0; i < 6U; i++) {
        uint8_t mac =
            rtl8139_in8(io + (uint16_t)i);

        rtl_tx_buffer[i] = 0xFF;
        rtl_tx_buffer[6U + i] = mac;
        rtl_tx_buffer[22U + i] = mac;
    }

    /* EtherType ARP */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x06;

    /* Ethernet + IPv4 */
    rtl_tx_buffer[14] = 0;
    rtl_tx_buffer[15] = 1;
    rtl_tx_buffer[16] = 0x08;
    rtl_tx_buffer[17] = 0x00;

    rtl_tx_buffer[18] = 6;
    rtl_tx_buffer[19] = 4;

    /* ARP Request */
    rtl_tx_buffer[20] = 0;
    rtl_tx_buffer[21] = 1;

    /* Sender IP: 10.0.2.15 */
    rtl_tx_buffer[28] = 10;
    rtl_tx_buffer[29] = 0;
    rtl_tx_buffer[30] = 2;
    rtl_tx_buffer[31] = 15;

    /* Target IP: 10.0.2.3 */
    rtl_tx_buffer[38] = 10;
    rtl_tx_buffer[39] = 0;
    rtl_tx_buffer[40] = 2;
    rtl_tx_buffer[41] = 3;

    __asm__ volatile("" ::: "memory");

   
/*
 * MiOS DNS ARP
 *
 * Secuencia:
 * TX0: ARP gateway
 * TX1: ICMP
 * TX2: UDP
 * TX3: ARP DNS
 *
 * TSAD3 = IO + 0x2C
 * TSD3  = IO + 0x1C
 */

__asm__ volatile("" ::: "memory");

/* Direccion DMA del paquete */
rtl8139_out32(
    io + 0x2C,
    (uint32_t)(uintptr_t)rtl_tx_buffer
);

/* Enviar 60 bytes por TX3 */
rtl8139_out32(
    io + 0x1C,
    60U
);

int sent = 0;

for (uint32_t n = 0;
     n < 5000000U;
     n++) {

    uint32_t tx =
        rtl8139_in32(io + 0x1C);

    /* TOK = transmision completada */
    if (tx & 0x00008000U) {
        sent = 1;
        break;
    }

    /* TABT = transmision abortada */
    if (tx & 0x40000000U)
        break;
}

if (!sent) {
    serial_write(
        "NET: DNS ARP TX3 ERROR TSD3="
    );

    serial_write_hex32(
        rtl8139_in32(io + 0x1C)
    );

    serial_write("\n");

    return -1;
}


    serial_write(
        "NET: ARP 10.0.2.3 ENVIADO\n"
    );

    for (uint32_t n = 0;
         n < 100000000U;
         n++) {

        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        uint32_t offset = *rx_offset;

        volatile uint8_t *packet =
            &rtl_rx_buffer[offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 46U ||
            length > 2048U ||
            offset + 4U + length >
                sizeof(rtl_rx_buffer)) {

            serial_write(
                "NET: DNS ARP RX ERROR\n"
            );
            return -1;
        }

        volatile uint8_t *frame =
            packet + 4;

        int found =
            frame[12] == 0x08 &&
            frame[13] == 0x06 &&
            frame[14] == 0x00 &&
            frame[15] == 0x01 &&
            frame[16] == 0x08 &&
            frame[17] == 0x00 &&
            frame[18] == 6 &&
            frame[19] == 4 &&
            frame[20] == 0 &&
            frame[21] == 2 &&
            frame[28] == 10 &&
            frame[29] == 0 &&
            frame[30] == 2 &&
            frame[31] == 3 &&
            frame[38] == 10 &&
            frame[39] == 0 &&
            frame[40] == 2 &&
            frame[41] == 15;

        if (found) {
            for (uint32_t i = 0; i < 6U; i++)
                dns_mac[i] = frame[22U + i];
        }

        /* Avanzar anillo RX */
        offset =
            (offset + 4U + length + 3U)
            & ~3U;

        offset &= 8191U;
        *rx_offset = offset;

        rtl8139_out16(
            io + 0x38,
            (uint16_t)(offset - 16U)
        );

        rtl8139_out16(
            io + 0x3E,
            0x0001U
        );

        if (found) {
            serial_write(
                "NET: DNS ARP 10.0.2.3 OK\n"
            );
            return 0;
        }
    }

    serial_write(
        "NET: DNS ARP TIMEOUT\n"
    );

    return -1;
}

/*
 * MiOS DNS: transmitir example.com
 * a 10.0.2.3:53 por RTL8139.
 */
static int rtl8139_dns_send(
    const uint8_t dns_mac[6]
) {
    if (!dns_mac || rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    uint8_t dns_query[128];

    const uint8_t src_ip[4] = {
        10, 0, 2, 15
    };

    const uint8_t dst_ip[4] = {
        10, 0, 2, 3
    };

    int dns_size = net_dns_build_query(
        dns_query,
        sizeof(dns_query),
        "example.com",
        0x4D49
    );

    if (dns_size != 29) {
        serial_write(
            "NET: DNS BUILD ERROR\n"
        );
        return -1;
    }

    for (uint32_t i = 0;
         i < sizeof(rtl_tx_buffer);
         i++) {
        rtl_tx_buffer[i] = 0;
    }

    /* Ethernet: MAC destino/origen */
    for (uint32_t i = 0; i < 6U; i++) {
        rtl_tx_buffer[i] = dns_mac[i];

        rtl_tx_buffer[6U + i] =
            rtl8139_in8(
                io + (uint16_t)i
            );
    }

    /* EtherType IPv4 */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x00;

    /* IPv4 + UDP + DNS */
    int ip_size = net_udp_build_ipv4(
        &rtl_tx_buffer[14],
        sizeof(rtl_tx_buffer) - 14U,
        src_ip,
        dst_ip,
        49153U,
        53U,
        dns_query,
        (uint16_t)dns_size
    );

    if (ip_size != 57) {
        serial_write(
            "NET: DNS IPV4 ERROR\n"
        );
        return -1;
    }

    uint32_t frame_size =
        14U + (uint32_t)ip_size;

    if (frame_size < 60U)
        frame_size = 60U;

    __asm__ volatile("" ::: "memory");

    /*
     * Descriptor TX3:
     * TSAD3 = IO + 0x2C
     * TSD3  = IO + 0x1C
     */
   /* TX0: vuelve a estar disponible
 * tras completar TX3.
 */
rtl8139_out32(
    io + 0x20,
    (uint32_t)(uintptr_t)rtl_tx_buffer
);

rtl8139_out32(io + 0x10, frame_size);

    for (uint32_t n = 0;
         n < 5000000U;
         n++) {

        uint32_t status =
    rtl8139_in32(io + 0x10);

        if (status & 0x00008000U) {
            serial_write(
                "NET: DNS QUERY example.com ENVIADA\n"
            );

            serial_write(
                "NET: DNS DESTINO 10.0.2.3:53\n"
            );

            serial_write(
                "NET: DNS TX OK\n"
            );

            return 0;
        }

        if (status & 0x40000000U)
            break;
    }

    serial_write(
        "NET: DNS TX ERROR\n"
    );

    return -1;
}

/*
 * MiOS DNS RX 0.4A.6
 * Funciones auxiliares.
 */

static uint16_t mios_dns_read16(
    const volatile uint8_t *p
) {
    return (uint16_t)(
        ((uint16_t)p[0] << 8) |
        (uint16_t)p[1]
    );
}

/* Imprimir numeros decimales por COM1 */
static void mios_dns_print_uint(
    uint32_t value
) {
    char buffer[11];
    char result[12];
    uint32_t n = 0;
    uint32_t j = 0;

    do {
        buffer[n++] =
            '0' + (char)(value % 10U);

        value /= 10U;
    } while (value && n < 10U);

    while (n) {
        result[j++] = buffer[--n];
    }

    result[j] = '\0';
    serial_write(result);
}

/*
 * Avanzar sobre un nombre DNS.
 *
 * Permite etiquetas normales y punteros
 * de compresion hacia posiciones anteriores.
 *
 * No expande el nombre; solo encuentra
 * el inicio del siguiente campo.
 */
static int mios_dns_skip_name(
    const volatile uint8_t *dns,
    uint32_t size,
    uint32_t *position
) {
    if (!dns || !position)
        return -1;

    uint32_t pos = *position;

    for (uint32_t steps = 0;
         steps < 128U;
         steps++) {

        if (pos >= size)
            return -1;

        uint8_t length = dns[pos];

        if (length == 0U) {
            *position = pos + 1U;
            return 0;
        }

        if ((length & 0xC0U) == 0xC0U) {
            if (pos + 2U > size)
                return -1;

            uint32_t target =
                ((uint32_t)(length & 0x3FU) << 8) |
                dns[pos + 1U];

            if (target < 12U ||
                target >= pos)
                return -1;

            *position = pos + 2U;
            return 0;
        }

        if (length & 0xC0U)
            return -1;

        if (length > 63U ||
            (uint32_t)length > size - pos - 1U)
            return -1;

        pos += 1U + (uint32_t)length;
    }

    return -1;
}

/*
 * Interpretar respuesta A de example.com.
 *
 * Retorna:
 *  0 = registro IPv4 encontrado
 * -1 = respuesta invalida o sin registro A
 */
static int mios_dns_parse_a(
    const volatile uint8_t *dns,
    uint32_t size,
    uint8_t ip_out[4]
) {
    if (!dns || !ip_out || size < 29U)
        return -1;

    /* La consulta enviada usa ID 0x4D49 */
    if (mios_dns_read16(dns) != 0x4D49U)
        return -1;

    uint16_t flags = mios_dns_read16(dns + 2);

    /* QR=1: respuesta; opcode=0; RCODE=0 */
    if ((flags & 0x8000U) == 0U ||
        (flags & 0x7800U) != 0U ||
        (flags & 0x000FU) != 0U)
        return -1;

    uint16_t questions =
        mios_dns_read16(dns + 4);

    uint16_t answers =
        mios_dns_read16(dns + 6);

    if (questions != 1U || answers == 0U)
        return -1;

    /*
     * Verificar pregunta exacta:
     * 07 example 03 com 00
     */
    static const uint8_t qname[] = {
        7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        3, 'c', 'o', 'm', 0
    };

    for (uint32_t i = 0;
         i < sizeof(qname);
         i++) {

        uint8_t actual = dns[12U + i];
        uint8_t expected = qname[i];

        /* Aceptar mayusculas en letras ASCII */
        if (actual >= 'A' && actual <= 'Z')
            actual = (uint8_t)(actual + 32U);

        if (actual != expected)
            return -1;
    }

    /* QTYPE=A, QCLASS=IN */
    if (mios_dns_read16(dns + 25) != 1U ||
        mios_dns_read16(dns + 27) != 1U)
        return -1;

    uint32_t pos = 29U;

    for (uint32_t i = 0;
         i < (uint32_t)answers;
         i++) {

        if (mios_dns_skip_name(
                dns, size, &pos
            ) != 0)
            return -1;

        /* TYPE + CLASS + TTL + RDLENGTH */
        if (pos > size ||
            size - pos < 10U)
            return -1;

        uint16_t type =
            mios_dns_read16(dns + pos);

        uint16_t dns_class =
            mios_dns_read16(dns + pos + 2U);

        uint16_t data_size =
            mios_dns_read16(dns + pos + 8U);

        pos += 10U;

        if (data_size > size - pos)
            return -1;

        /* Registro A, clase IN, 4 bytes */
        if (type == 1U &&
            dns_class == 1U &&
            data_size == 4U) {

            for (uint32_t j = 0; j < 4U; j++)
                ip_out[j] = dns[pos + j];

            return 0;
        }

        pos += data_size;
    }

    return -1;
}

/*
 * MiOS Network 0.4A.6
 * Recibir respuesta DNS por UDP.
 */
static int rtl8139_dns_receive(
    uint32_t *rx_offset
) {
    if (!rx_offset || rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    serial_write(
        "NET: ESPERANDO RESPUESTA DNS...\n"
    );

    for (uint32_t n = 0;
         n < 100000000U;
         n++) {

        /* No hay paquetes disponibles */
        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        uint32_t offset = *rx_offset;

        volatile uint8_t *packet =
            &rtl_rx_buffer[offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 4U ||
            length > 2048U ||
            offset + 4U + length >
                sizeof(rtl_rx_buffer)) {

            serial_write(
                "NET: DNS RX PACKET ERROR\n"
            );

            return -1;
        }

        volatile uint8_t *frame = packet + 4;

        /* Los ultimos 4 bytes son FCS */
        uint32_t frame_size =
            (uint32_t)length - 4U;

        int found = 0;
        uint8_t result_ip[4] = {0, 0, 0, 0};

        if (frame_size >= 42U &&
            frame[12] == 0x08 &&
            frame[13] == 0x00) {

            volatile uint8_t *ip =
                frame + 14;

            uint32_t ihl =
                (uint32_t)(ip[0] & 15U) * 4U;

            uint32_t ip_total =
                ((uint32_t)ip[2] << 8) |
                ip[3];

            uint16_t fragment =
                mios_dns_read16(ip + 6);

            if ((ip[0] >> 4) == 4U &&
                ihl >= 20U &&
                ihl <= 60U &&
                frame_size >= 14U + ihl + 8U &&
                ip_total >= ihl + 8U &&
                ip_total <= frame_size - 14U &&
                (fragment & 0x3FFFU) == 0U &&
                ip[9] == 17U &&

                /* Servidor DNS 10.0.2.3 */
                ip[12] == 10 &&
                ip[13] == 0 &&
                ip[14] == 2 &&
                ip[15] == 3 &&

                /* MiOS 10.0.2.15 */
                ip[16] == 10 &&
                ip[17] == 0 &&
                ip[18] == 2 &&
                ip[19] == 15) {

                volatile uint8_t *udp =
                    ip + ihl;

                uint16_t source_port =
                    mios_dns_read16(udp);

                uint16_t destination_port =
                    mios_dns_read16(udp + 2);

                uint16_t udp_length =
                    mios_dns_read16(udp + 4);

                if (source_port == 53U &&
                    destination_port == 49153U &&
                    udp_length >= 8U + 29U &&
                    udp_length <= ip_total - ihl) {

                    volatile uint8_t *dns =
                        udp + 8;

                    uint32_t dns_size =
                        (uint32_t)udp_length - 8U;

                    if (mios_dns_parse_a(
                            dns,
                            dns_size,
                            result_ip
                        ) == 0) {

                        found = 1;
                    }
                }
            }
        }

        /*
         * Liberar el paquete del anillo RX,
         * aunque no sea la respuesta buscada.
         */
        offset =
            (offset + 4U + length + 3U)
            & ~3U;

        offset &= 8191U;
        *rx_offset = offset;

        /* CAPR */
        rtl8139_out16(
            io + 0x38,
            (uint16_t)(offset - 16U)
        );

        /* Confirmacion de recepcion */
        rtl8139_out16(
            io + 0x3E,
            0x0001U
        );

        if (found) {
            serial_write(
                "NET: DNS RESPUESTA RECIBIDA!\n"
            );

            serial_write(
                "NET: DNS example.com RESUELTO\n"
            );

            serial_write(
                "NET: DNS IPv4 = "
            );

            for (uint32_t j = 0; j < 4U; j++) {
                mios_dns_print_uint(result_ip[j]);

                if (j != 3U)
                    serial_write(".");
            }

            serial_write("\n");
            serial_write("NET: DNS RX OK\n");

            return 0;
        }
    }

    serial_write("NET: DNS RX TIMEOUT\n");

    return -1;
}

/*
 * MiOS TCP 0.2
 * Enviar SYN hacia el servidor
 * TCP de Ubuntu mediante RTL8139.
 *
 * Se ejecuta despues de:
 * TX0 ARP gateway
 * TX1 ICMP
 * TX2 UDP
 * TX3 ARP DNS
 * TX0 consulta DNS
 *
 * Siguiente descriptor: TX1.
 */
static int rtl8139_tcp_send_syn(
    const uint8_t gateway_mac[6]
) {
    if (!gateway_mac || rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    const uint8_t src_ip[4] = {
        10, 0, 2, 15
    };

    const uint8_t dst_ip[4] = {
        10, 0, 2, 2
    };

    /* Limpiar buffer de transmision */
    for (uint32_t i = 0;
         i < sizeof(rtl_tx_buffer);
         i++) {
        rtl_tx_buffer[i] = 0;
    }

    /* Cabecera Ethernet */
    for (uint32_t i = 0; i < 6U; i++) {
        rtl_tx_buffer[i] = gateway_mac[i];

        rtl_tx_buffer[6U + i] =
            rtl8139_in8(
                io + (uint16_t)i
            );
    }

    /* EtherType IPv4 */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x00;

    /* Construir IPv4 + TCP SYN */
    int ip_size = net_tcp_build_syn(
        &rtl_tx_buffer[14],
        sizeof(rtl_tx_buffer) - 14U,
        src_ip,
        dst_ip,
        49154U,
        8080U,
        0x12345678U
    );

    if (ip_size != 40) {
        serial_write(
            "NET: TCP SYN BUILD ERROR\n"
        );
        return -1;
    }

    uint32_t frame_size =
        14U + (uint32_t)ip_size;

    /* Longitud minima Ethernet sin FCS */
    if (frame_size < 60U)
        frame_size = 60U;

    __asm__ volatile("" ::: "memory");

    /*
     * TX1 de RTL8139
     * TSAD1 = IO + 0x24
     * TSD1  = IO + 0x14
     */
    rtl8139_out32(
        io + 0x24,
        (uint32_t)(uintptr_t)rtl_tx_buffer
    );

    rtl8139_out32(
        io + 0x14,
        frame_size
    );

    for (uint32_t n = 0;
         n < 5000000U;
         n++) {

        uint32_t status =
            rtl8139_in32(io + 0x14);

        /* TOK: transmision completada */
        if (status & 0x00008000U) {
            serial_write(
                "NET: TCP SYN ENVIADO\n"
            );

            serial_write(
                "NET: TCP DESTINO 10.0.2.2:8080\n"
            );

            serial_write(
                "NET: TCP TX OK\n"
            );

            return 0;
        }

        /* TABT: transmision abortada */
        if (status & 0x40000000U) {
            serial_write(
                "NET: TCP TX ABORTADA\n"
            );
            return -1;
        }
    }

    serial_write(
        "NET: TCP TX TIMEOUT\n"
    );

    return -1;
}

/*
 * MiOS Network TCP 0.3
 *
 * Leer SYN-ACK del anillo RX RTL8139.
 * No envia el ACK final todavia.
 */

static uint16_t mios_tcp_read16(
    const volatile uint8_t *p
) {
    return (uint16_t)(
        ((uint16_t)p[0] << 8) |
        (uint16_t)p[1]
    );
}

static uint32_t mios_tcp_read32(
    const volatile uint8_t *p
) {
    return
        ((uint32_t)p[0] << 24) |
        ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) |
        (uint32_t)p[3];
}

/*
 * Validar checksum TCP usando la
 * pseudo-cabecera IPv4.
 *
 * Devuelve 1 si es correcto.
 */
static int mios_tcp_checksum_valid(
    const volatile uint8_t *ip,
    const volatile uint8_t *tcp,
    uint32_t tcp_len
) {
    uint32_t sum = 0;

    /* Direcciones IP de la pseudo-cabecera */
    for (uint32_t i = 12; i < 20; i += 2) {
        sum +=
            ((uint32_t)ip[i] << 8) |
            (uint32_t)ip[i + 1];
    }

    sum += 6U;       /* Protocolo TCP */
    sum += tcp_len;  /* Longitud TCP */

    for (uint32_t i = 0; i < tcp_len; i += 2) {
        uint16_t word =
            (uint16_t)tcp[i] << 8;

        if (i + 1 < tcp_len)
            word |= tcp[i + 1];

        sum += word;
    }

    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);

    return ((uint16_t)sum == 0xFFFFU);
}

/*
 * Salida:
 * server_next_seq = SEQ del SYN-ACK + 1
 *
 * Guarda informacion necesaria para
 * preparar el ACK en TCP 0.4.
 */
static int rtl8139_tcp_receive_synack(
    uint32_t *rx_offset,
    uint32_t *server_next_seq
) {
    if (!rx_offset ||
        !server_next_seq ||
        rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    serial_write(
        "NET: ESPERANDO TCP SYN-ACK...\n"
    );

    for (uint32_t n = 0;
         n < 100000000U;
         n++) {

        /* BUFE=1: anillo RX vacio */
        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        uint32_t offset = *rx_offset;

        volatile uint8_t *packet =
            &rtl_rx_buffer[offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 4U ||
            length > 2048U ||
            offset + 4U + length >
                sizeof(rtl_rx_buffer)) {

            serial_write(
                "NET: TCP RX PACKET ERROR\n"
            );
            return -1;
        }

        volatile uint8_t *frame = packet + 4;
        uint32_t frame_size = length - 4U;

        int synack_ok = 0;
        uint32_t next_seq = 0;

        /* Ethernet + IPv4 + TCP minimo */
        if (frame_size >= 54U &&
            frame[12] == 0x08 &&
            frame[13] == 0x00) {

            volatile uint8_t *ip =
                frame + 14;

            uint32_t ihl =
                (uint32_t)(ip[0] & 15U) * 4U;

            uint32_t ip_total =
                mios_tcp_read16(ip + 2);

            uint16_t fragment =
                mios_tcp_read16(ip + 6);

            if ((ip[0] >> 4) == 4U &&
                ihl >= 20U &&
                ihl <= 60U &&
                frame_size >= 14U + ihl + 20U &&
                ip_total >= ihl + 20U &&
                ip_total <= frame_size - 14U &&
                (fragment & 0x3FFFU) == 0U &&
                ip[9] == 6U &&

                /* IP servidor 10.0.2.2 */
                ip[12] == 10 &&
                ip[13] == 0 &&
                ip[14] == 2 &&
                ip[15] == 2 &&

                /* IP MiOS 10.0.2.15 */
                ip[16] == 10 &&
                ip[17] == 0 &&
                ip[18] == 2 &&
                ip[19] == 15) {

                volatile uint8_t *tcp =
                    ip + ihl;

                uint32_t tcp_len =
                    ip_total - ihl;

                uint32_t tcp_header_len =
                    ((uint32_t)tcp[12] >> 4) * 4U;

                uint16_t src_port =
                    mios_tcp_read16(tcp);

                uint16_t dst_port =
                    mios_tcp_read16(tcp + 2);

                uint32_t server_seq =
                    mios_tcp_read32(tcp + 4);

                uint32_t server_ack =
                    mios_tcp_read32(tcp + 8);

                uint8_t flags = tcp[13];

                /*
                 * SYN + ACK deben estar activos.
                 * RST y FIN deben estar apagados.
                 */
                if (tcp_header_len >= 20U &&
                    tcp_header_len <= tcp_len &&
                    src_port == 8080U &&
                    dst_port == 49154U &&
                    (flags & 0x12U) == 0x12U &&
                    (flags & 0x05U) == 0U &&
                    server_ack == 0x12345679U &&
                    mios_tcp_checksum_valid(
                        ip,
                        tcp,
                        tcp_len
                    )) {

                    next_seq = server_seq + 1U;
                    synack_ok = 1;
                }
            }
        }

        /*
         * Consumir el paquete del anillo RX.
         * Mantener la posicion compartida.
         */
        offset =
            (offset + 4U + length + 3U)
            & ~3U;

        offset &= 8191U;
        *rx_offset = offset;

        rtl8139_out16(
            io + 0x38,
            (uint16_t)(offset - 16U)
        );

        rtl8139_out16(
            io + 0x3E,
            0x0001U
        );

        if (synack_ok) {
            *server_next_seq = next_seq;

            serial_write(
                "NET: TCP SYN-ACK RECIBIDO!\n"
            );

            serial_write(
                "NET: TCP PUERTOS OK\n"
            );

            serial_write(
                "NET: TCP SEQUENCE/ACK OK\n"
            );

            serial_write(
                "NET: TCP CHECKSUM RX OK\n"
            );

            serial_write(
                "NET: TCP SYN-ACK VALIDADO\n"
            );

            serial_write(
                "NET: TCP SERVER NEXT SEQ = "
            );

            serial_write_hex32(next_seq);
            serial_write("\n");

            return 0;
        }
    }

    serial_write(
        "NET: TCP SYN-ACK TIMEOUT\n"
    );

    return -1;
}

/*
 * MiOS TCP 0.4
 *
 * Enviar ACK final del handshake.
 *
 * En esta secuencia de laboratorio,
 * TX2 sigue al TX1 usado por SYN.
 */
static int rtl8139_tcp_send_ack(
    const uint8_t gateway_mac[6],
    uint32_t server_next_seq
) {
    if (!gateway_mac ||
        rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    const uint8_t src_ip[4] = {
        10, 0, 2, 15
    };

    const uint8_t dst_ip[4] = {
        10, 0, 2, 2
    };

    /* Limpiar TX DMA */
    for (uint32_t i = 0;
         i < sizeof(rtl_tx_buffer);
         i++) {
        rtl_tx_buffer[i] = 0;
    }

    /* Ethernet: MAC destino y origen */
    for (uint32_t i = 0; i < 6U; i++) {
        rtl_tx_buffer[i] = gateway_mac[i];

        rtl_tx_buffer[6U + i] =
            rtl8139_in8(
                io + (uint16_t)i
            );
    }

    /* EtherType IPv4 */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x00;

    /*
     * SYN consumio un numero
     * de secuencia.
     *
     * SEQ = 0x12345679
     * ACK = server_next_seq
     */
    int ip_size = net_tcp_build_ack(
        &rtl_tx_buffer[14],
        sizeof(rtl_tx_buffer) - 14U,
        src_ip,
        dst_ip,
        49154U,
        8080U,
        0x12345679U,
        server_next_seq
    );

    if (ip_size != 40) {
        serial_write(
            "NET: TCP ACK BUILD ERROR\n"
        );
        return -1;
    }

    uint32_t frame_size =
        14U + (uint32_t)ip_size;

    /* Minimo Ethernet sin FCS */
    if (frame_size < 60U)
        frame_size = 60U;

    if (frame_size > sizeof(rtl_tx_buffer))
        return -1;

    __asm__ volatile("" ::: "memory");

    /*
     * RTL8139 TX2
     *
     * TSAD2 = IO + 0x28
     * TSD2  = IO + 0x18
     */
    rtl8139_out32(
        io + 0x28,
        (uint32_t)(uintptr_t)rtl_tx_buffer
    );

    rtl8139_out32(
        io + 0x18,
        frame_size
    );

    for (uint32_t n = 0;
         n < 5000000U;
         n++) {

        uint32_t status =
            rtl8139_in32(io + 0x18);

        if (status & 0x00008000U) {
            serial_write(
                "NET: TCP ACK ENVIADO\n"
            );

            serial_write(
                "NET: TCP ACK TX OK\n"
            );

            return 0;
        }

        if (status & 0x40000000U) {
            serial_write(
                "NET: TCP ACK ABORTADO\n"
            );
            return -1;
        }
    }

    serial_write(
        "NET: TCP ACK TIMEOUT\n"
    );

    return -1;
}

static int rtl8139_dma_arp_test(void)
{
    if (rtl8139_io_base == 0)
        return -1;

    uint16_t io = rtl8139_io_base;

    /*
     * En MiOS, el kernel utiliza un mapeo
     * de identidad para los primeros 128 MiB.
     */
    uint32_t rx_phys =
        (uint32_t)(uintptr_t)&rtl_rx_buffer[0];

    uint32_t tx_phys =
        (uint32_t)(uintptr_t)&rtl_tx_buffer[0];

    if ((rx_phys & 4095U) != 0 ||
        (tx_phys & 4095U) != 0 ||
        rx_phys >= 0x08000000U - sizeof(rtl_rx_buffer) ||
        tx_phys >= 0x08000000U - sizeof(rtl_tx_buffer))
    {
        serial_write("NET: DMA ADDRESS ERROR\n");
        return -1;
    }

    /* Inicializar buffers */
    for (uint32_t i = 0; i < sizeof(rtl_rx_buffer); i++)
        rtl_rx_buffer[i] = 0;

    for (uint32_t i = 0; i < sizeof(rtl_tx_buffer); i++)
        rtl_tx_buffer[i] = 0;

    /* Mantener interrupciones de red desactivadas */
    rtl8139_out16(io + 0x3C, 0);
    rtl8139_out16(io + 0x3E, 0xFFFF);

    /* Configurar el buffer de recepcion */
    rtl8139_out32(io + 0x30, rx_phys);
    rtl8139_out16(io + 0x38, 0xFFF0);

    /*
     * RX: 8 KiB, WRAP habilitado,
     * aceptar broadcast y MAC propia.
     */
    rtl8139_out32(io + 0x44, 0x0000008AU);

    /* Activar RX y TX */
    rtl8139_out8(io + 0x37, 0x0C);

    if ((rtl8139_in8(io + 0x37) & 0x0CU) != 0x0CU) {
        serial_write("NET: RX/TX ENABLE ERROR\n");
        return -1;
    }

    serial_write("NET: DMA RX/TX CONFIGURADO\n");

    /*
     * Construir una solicitud ARP:
     * ¿Quien tiene 10.0.2.2?
     *
     * IP experimental MiOS: 10.0.2.15
     */

    for (int i = 0; i < 6; i++) {
        uint8_t mac = rtl8139_in8(io + (uint16_t)i);

        rtl_tx_buffer[i] = 0xFF;
        rtl_tx_buffer[6 + i] = mac;
        rtl_tx_buffer[22 + i] = mac;
    }

    /* EtherType ARP */
    rtl_tx_buffer[12] = 0x08;
    rtl_tx_buffer[13] = 0x06;

    /* Hardware Ethernet */
    rtl_tx_buffer[14] = 0x00;
    rtl_tx_buffer[15] = 0x01;

    /* Protocolo IPv4 */
    rtl_tx_buffer[16] = 0x08;
    rtl_tx_buffer[17] = 0x00;

    /* Longitudes MAC e IP */
    rtl_tx_buffer[18] = 6;
    rtl_tx_buffer[19] = 4;

    /* ARP Request */
    rtl_tx_buffer[20] = 0;
    rtl_tx_buffer[21] = 1;

    /* IP de MiOS: 10.0.2.15 */
    rtl_tx_buffer[28] = 10;
    rtl_tx_buffer[29] = 0;
    rtl_tx_buffer[30] = 2;
    rtl_tx_buffer[31] = 15;

    /* MAC destino ARP: 00:00:00:00:00:00 */

    /* IP destino: 10.0.2.2 */
    rtl_tx_buffer[38] = 10;
    rtl_tx_buffer[39] = 0;
    rtl_tx_buffer[40] = 2;
    rtl_tx_buffer[41] = 2;

    /* Asegurar que el buffer este listo para DMA */
    __asm__ volatile("" ::: "memory");

    /* Descriptor de transmision 0 */
    rtl8139_out32(io + 0x20, tx_phys);

    /* Enviar 60 bytes, sin incluir el CRC */
    rtl8139_out32(io + 0x10, 60U);

    int tx_ok = 0;

    for (uint32_t i = 0; i < 5000000U; i++) {
        uint32_t status = rtl8139_in32(io + 0x10);

        if (status & 0x00008000U) {
            tx_ok = 1;
            break;
        }
    }

    if (!tx_ok) {
        serial_write("NET: ARP TX ERROR, TSD0=");
        serial_write_hex32(rtl8139_in32(io + 0x10));
        serial_write("\n");
        return -1;
    }

    serial_write("NET: ARP REQUEST ENVIADA\n");
    serial_write("NET: ESPERANDO RESPUESTA...\n");

    uint32_t rx_offset = 0;

    for (uint32_t wait = 0; wait < 10000000U; wait++) {

        /* Bit BUFE = 1: aun no hay paquetes */
        if (rtl8139_in8(io + 0x37) & 1U)
            continue;

        volatile uint8_t *packet =
            (volatile uint8_t *)&rtl_rx_buffer[rx_offset];

        uint16_t status =
            (uint16_t)packet[0] |
            ((uint16_t)packet[1] << 8);

        uint16_t length =
            (uint16_t)packet[2] |
            ((uint16_t)packet[3] << 8);

        if (!(status & 1U) ||
            length < 4 ||
            length > 2048U ||
            rx_offset + 4U + length > sizeof(rtl_rx_buffer))
        {
            serial_write("NET: RX PACKET ERROR\n");
            return -1;
        }

        volatile uint8_t *frame = packet + 4;

        /* Identificar respuesta ARP de 10.0.2.2 */
        int arp_reply =
            length >= 46 &&
            frame[12] == 0x08 &&
            frame[13] == 0x06 &&
            frame[20] == 0x00 &&
            frame[21] == 0x02 &&
            frame[28] == 10 &&
            frame[29] == 0 &&
            frame[30] == 2 &&
            frame[31] == 2;

        /* Liberar el espacio usado en el anillo RX */
        rx_offset =
            (rx_offset + 4U + length + 3U) & ~3U;

        rx_offset &= 8191U;

        rtl8139_out16(
            io + 0x38,
            (uint16_t)(rx_offset - 16U)
        );

        /* Confirmar recepcion */
        rtl8139_out16(io + 0x3E, 0x0001U);

        if (arp_reply) {
    uint8_t gateway_mac[6];

    for (int k = 0; k < 6; k++)
        gateway_mac[k] = frame[22 + k];

   
serial_write(
    "NET: RESPUESTA ARP 10.0.2.2 RECIBIDA!\n"
);

/* Primero comprobar ICMP */
if (rtl8139_ping_gateway(
        gateway_mac,
        &rx_offset
    ) != 0) {

    serial_write(
        "NET: PING FALLO, UDP CANCELADO\n"
    );
    return -1;
}

/* Despues transmitir UDP */
serial_write(
    "NET: PREPARANDO UDP REAL\n"
);


/* Enviar datagrama UDP */
if (rtl8139_udp_send_gateway(
        gateway_mac
    ) != 0) {

    serial_write(
        "NET: UDP TX ERROR\n"
    );
    return -1;
}

serial_write(
    "NET: UDP TRANSMISION COMPLETADA\n"
);

/* Esperar la respuesta de Ubuntu */
if (rtl8139_udp_receive(
        &rx_offset
    ) != 0) {

    serial_write(
        "NET: UDP RX FALLO\n"
    );
    return -1;
}


serial_write(
    "NET: UDP INTERCAMBIO COMPLETO!\n"
);

/* Resolver MAC del DNS 10.0.2.3 */
uint8_t dns_mac[6];

if (rtl8139_dns_arp(
        dns_mac,
        &rx_offset
    ) != 0) {

    serial_write(
        "NET: DNS ARP FALLO\n"
    );
    return -1;
}

/* Enviar consulta DNS real */

if (rtl8139_dns_send(dns_mac) != 0) {
    serial_write(
        "NET: DNS CONSULTA FALLO\n"
    );
    return -1;
}

serial_write(
    "NET: DNS CONSULTA TRANSMITIDA\n"
);

/* Leer respuesta DNS del mismo anillo RX */
if (rtl8139_dns_receive(
        &rx_offset
    ) != 0) {

    serial_write(
        "NET: DNS RESOLUCION FALLO\n"
    );

    return -1;
}

serial_write(
    "NET: DNS RESOLUCION COMPLETA!\n"
);

/*
 * MiOS TCP 0.2
 * Primer SYN real.
 *
 * gateway_mac fue obtenido
 * durante la resolucion ARP inicial.
 */
serial_write(
    "NET: PREPARANDO TCP SYN REAL\n"
);

if (rtl8139_tcp_send_syn(
        gateway_mac
    ) != 0) {

    serial_write(
        "NET: TCP SYN ERROR\n"
    );

    return -1;
}

serial_write(
    "NET: TCP 0.2 TRANSMISION COMPLETA\n"
);

    /*
     * MiOS Network TCP 0.3
     * Recibir y validar SYN-ACK.
     */

    uint32_t tcp_server_next_seq = 0;

    serial_write(
        "NET: ESPERANDO HANDSHAKE TCP 0.3\n"
    );

    if (rtl8139_tcp_receive_synack(
            &rx_offset,
            &tcp_server_next_seq
        ) != 0) {

        serial_write(
            "NET: TCP HANDSHAKE RX FALLO\n"
        );

        return -1;
    }

    serial_write(
        "NET: TCP 0.3 COMPLETADO\n"
    );

    /*
     * Informacion para TCP 0.4:
     *
     * SEQ = 0x12345679
     * ACK = tcp_server_next_seq
     *
     * En la siguiente etapa
     * enviaremos el ACK final.
     */

    /*
     * MiOS TCP 0.4
     * Enviar ACK final del handshake.
     */

    serial_write(
        "NET: PREPARANDO TCP ACK FINAL\n"
    );

    if (rtl8139_tcp_send_ack(
            gateway_mac,
            tcp_server_next_seq
        ) != 0) {

        serial_write(
            "NET: TCP HANDSHAKE ACK FALLO\n"
        );

        return -1;
    }

    serial_write(
        "NET: TCP HANDSHAKE ACK TRANSMITIDO\n"
    );

return 0;




}
    }

    serial_write("NET: SIN RESPUESTA ARP\n");
    return -1;
}

void kernel_main(uint32_t multiboot_magic,
    uint32_t multiboot_info) {
        
/* Consola de diagnostico independiente de VGA */
serial_init();

/* MiOS Network TCP 0.1 */

serial_write(
    "\n=== MiOS TCP 0.1 ===\n"
);

if (net_tcp_selftest() == 0) {
    serial_write(
        "NET: TCP IPV4 HEADER OK\n"
    );

    serial_write(
        "NET: TCP SYN HEADER OK\n"
    );

    serial_write(
        "NET: TCP SEQUENCE NUMBER OK\n"
    );

    serial_write(
        "NET: TCP IP CHECKSUM OK\n"
    );

    serial_write(
        "NET: TCP CHECKSUM OK\n"
    );

    serial_write(
        "NET: TCP SELFTEST OK\n"
    );
} else {
    serial_write(
        "NET: TCP SELFTEST ERROR\n"
    );
}

/* MiOS Network 0.4A.4 - DNS */

serial_write(
    "\n=== MiOS DNS 0.1 ===\n"
);

if (net_dns_selftest() == 0) {
    serial_write(
        "NET: DNS HEADER OK\n"
    );
    serial_write(
        "NET: DNS QNAME OK\n"
    );
    serial_write(
        "NET: DNS TYPE A OK\n"
    );
    serial_write(
        "NET: DNS SELFTEST OK\n"
    );
} else {
    serial_write(
        "NET: DNS SELFTEST ERROR\n"
    );
}

if (mios_dns_packet_test() == 0) {
    serial_write(
        "NET: DNS 0.2 PACKET READY\n"
    );
} else {
    serial_write(
        "NET: DNS 0.2 PACKET ERROR\n"
    );
}


serial_write("\n=== MiOS NETWORK 0.4A ===\n");

if (net_udp_selftest() == 0) {
    serial_write("NET: IPv4 HEADER OK\n");
    serial_write("NET: UDP HEADER OK\n");
    serial_write("NET: IP CHECKSUM OK\n");
    serial_write("NET: UDP PAYLOAD OK\n");
    serial_write("NET: UDP SELFTEST OK\n");
} else {
    serial_write("NET: UDP SELFTEST ERROR\n");
}

mios_network_detect();
if (rtl8139_init_basic() == 0) {
    rtl8139_dma_arp_test();
}
serial_write("\n");
serial_write("=== MiOS SERIAL DEBUG ===\n");
serial_write("Kernel iniciado.\n");

serial_write("Multiboot magic: ");
serial_write_hex32(multiboot_magic);
serial_write("\n");

        mbi_saved_address = multiboot_info;
        framebuffer_probe(multiboot_magic, multiboot_info);
        
const MiosFramebufferInfo *boot_fb =
    framebuffer_get_info();

serial_write("Framebuffer: ");

if (!boot_fb->available) {
    serial_write("NO DISPONIBLE\n");

} else {
    serial_write("DETECTADO\n");

    serial_write("Direccion: ");
    serial_write_hex32(boot_fb->address_low);
    serial_write("\n");

    serial_write("Ancho: ");
    serial_write_hex32(boot_fb->width);
    serial_write("\n");

    serial_write("Alto: ");
    serial_write_hex32(boot_fb->height);
    serial_write("\n");

    serial_write("BPP: ");
    serial_write_hex32(boot_fb->bpp);
    serial_write("\n");

    serial_write("Tipo: ");
    serial_write_hex32(boot_fb->type);
    serial_write("\n");
}

            memory_map_init(multiboot_magic, multiboot_info);
            int pmm_status = pmm_init();
(void)pmm_status;

paging_init();

/*
 * Preparar la region grafica antes
 * de activar la paginacion.
 */
if (framebuffer_map_prepare() == 0) {
    serial_write("FB MAP: PREPARADO\n");

    serial_write("Virtual: ");
    serial_write_hex32(
        framebuffer_map_address()
    );
    serial_write("\n");

    serial_write("Bytes: ");
    serial_write_hex32(
        framebuffer_map_size()
    );
    serial_write("\n");

} else {
    serial_write("FB MAP: NO DISPONIBLE\n");
}

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
 
#if MIOS_FB_DEMO

serial_write("\n");
serial_write("=== MIOS PIXEL DEMO ===\n");

if (!framebuffer_map_ready()) {
    serial_write("ERROR: framebuffer sin mapeo.\n");

} else {
    serial_write("FB MAP: OK\n");

    serial_write("Activando paginacion...\n");

    int pg_result = paging_enable();

    if (pg_result != 0 ||
        !paging_cpu_enabled()) {

        serial_write("ERROR: paginacion.\n");

    } else {
        serial_write("PAGING: OK\n");

        serial_write("Directorio CR3: ");
        serial_write_hex32(
            paging_directory_address()
        );
        serial_write("\n");

        serial_write("Preparando RGB...\n");

        if (fb_draw_demo() == 0) {
            serial_write("FB DRAW: OK\n");
            serial_write("PIXEL DEMO: OK\n");
        } else {
            serial_write("FB DRAW: ERROR\n");
        }
    }
}

/*
 * Detener aqui la demostracion.
 * El resto de MiOS queda intacto,
 * pero no iniciamos la consola VGA.
 */

serial_write("Inicializando raton PS/2...\n");

if (mouse_init() == 0) {
    serial_write("MOUSE PS2: OK\n");
} else {
    serial_write("MOUSE PS2: ERROR\n");
    serial_write("Continuando con teclado.\n");
}

serial_write("MiOS Desktop interactivo.\n");

/* Ventana seleccionada */
uint32_t desktop_window = 0U;

/* Prefijo de tecla extendida PS/2 */
int desktop_extended = 0;

/* Dibujar la pantalla inicial */
if (desktop_render_app(desktop_window) != 0) {
    serial_write("ERROR: dibujando escritorio.\n");
} else {
    serial_write("DESKTOP READY: OK\n");
}

/*
 * Bucle experimental de entrada.
 *
 * Utiliza el teclado PS/2 directamente,
 * sin iniciar la consola VGA.
 */
int last_hover = -1;
/*
 * MiOS Desktop 0.6.2
 * Estado anterior del boton izquierdo.
 */
for (;;) {
     int redraw = 0;
    uint8_t code = 0;

    /*
     * Leer el controlador PS/2
     * identificando el dispositivo.
     */
    int device = ps2_input_read(&code);

    if (device == PS2_INPUT_NONE) {
        __asm__ volatile("pause");
        continue;
    }

/*
 * Diagnostico temporal del teclado.
 */
if (device == PS2_INPUT_KEYBOARD) {
    serial_write("PS2 KEYBOARD: ");
    serial_write_hex32((uint32_t)code);
    serial_write("\n");
}

    /*
     * Los datos del raton no deben
     * interpretarse como teclado.
     *
     * Todavia no activamos el raton.
     */
    
if (device == PS2_INPUT_MOUSE) {
    static uint32_t mouse_samples = 0;
    static uint8_t old_left = 0;

    MouseState state;

    mouse_handle_byte(code);
    mouse_get_state(&state);
    
    static uint32_t last_packet = 0;

        if (!state.available)
    continue;

if (state.packet_count == last_packet)
    continue;

last_packet = state.packet_count;

/* Detectar una pulsacion NUEVA del boton izquierdo */


/*
 * Redibujar el escritorio
 * y colocar el cursor encima.
 */

/*
 * Identificar opcion bajo el cursor.
 */
int hover = desktop_mouse_hit(
    state.x,
    state.y
);



/*
 * MiOS Desktop 0.6
 * Ejecutar acciones solo con clic izquierdo.
*/

/* Dibujar el cursor nuevamente */
fb_cursor_show(
    (int)state.x,
    (int)state.y
);

/*if (desktop_render_app(desktop_window); == 0) {
    fb_draw_cursor(
        (int)state.x,
        (int)state.y
    );
}*/


    if (!state.available)
        continue;

    /*
     * Mostrar una muestra cada
     * 32 bytes recibidos, sin
     * inundar el puerto serie.
     */
    mouse_samples++;

    if ((mouse_samples % 32U) == 0U) {
        serial_write("MOUSE X: ");
        serial_write_hex32((uint32_t)state.x);

        serial_write(" Y: ");
        serial_write_hex32((uint32_t)state.y);

        serial_write("\n");
    }

    /* Detectar una nueva pulsacion */
   
if (state.left && !old_left) {

    serial_write("MOUSE: CLICK IZQUIERDO\n");

    serial_write("CLICK X: ");
    serial_write_hex32((uint32_t)state.x);

    serial_write(" Y: ");
    serial_write_hex32((uint32_t)state.y);

    serial_write("\n");

    /*
     * Averiguar si se pulso
     * una opcion del menu.
     */
  
int selected = -1;

/* Cerrar terminal pulsando X */
if (desktop_window == MIOS_APP_TERMINAL &&
    state.x >= 564 && state.x < 586 &&
    state.y >= 129 && state.y < 147)
{
    selected = 0;
}

else
{
    /* Solo dock inferior */
    selected = -1;

    if (state.y >= 433 && state.y < 470) {
        if (state.x >= 199 && state.x < 247)
            selected = 0;  /* Inicio */
        else if (state.x >= 263 && state.x < 311)
            selected = 5;  /* Terminal */
        else if (state.x >= 327 && state.x < 375)
            selected = 3;  /* Archivos */
        else if (state.x >= 391 && state.x < 439)
            selected = 4;  /* Sistema */
    }

    /* Iconos del escritorio */
    if (selected < 0 && desktop_window == 0) {
        selected = desktop_app_hit(
            (int)state.x,
            (int)state.y
        );
    }
}

    if (selected >= 0) {
fb_cursor_hide();

desktop_window = (uint32_t)selected;

if (desktop_render_app(desktop_window) == 0) {
    fb_cursor_show(
        (int)state.x,
        (int)state.y
    );
}
if (redraw) {
    MouseState current_mouse;
    mouse_get_state(&current_mouse);

    fb_cursor_hide();

    if (desktop_render_app(desktop_window) != 0) {
        serial_write("ERROR: cambio de ventana.\n");
    }

    if (current_mouse.available) {
        fb_cursor_show(
            (int)current_mouse.x,
            (int)current_mouse.y
        );
    }
}
        desktop_window =
            (uint32_t)selected;

        serial_write("MENU: ventana ");
        serial_write_hex32(desktop_window);
        serial_write("\n");

        /*
         * Actualizar inmediatamente
         * el escritorio.
         */
        if (fb_desktop_draw_live(
                desktop_window
            ) != 0) {

            serial_write(
                "ERROR: redibujando ventana\n"
            );

        } else {
            serial_write(
                "MOUSE: VENTANA ACTUALIZADA\n"
            );
        }

    } else {
        serial_write(
            "MOUSE: CLICK FUERA DEL MENU\n"
        );
    }
}


    old_left = state.left;
    
/* Teclado exclusivo de Terminal */


    continue;
}

/* Teclado de la Terminal MiOS */
if (device == PS2_INPUT_KEYBOARD &&
    desktop_window == MIOS_APP_TERMINAL)
{
    if (term_handle_key(code)) {
        MouseState mouse_now;
        mouse_get_state(&mouse_now);

        fb_cursor_hide();
        term_draw_content();

        if (mouse_now.available) {
            fb_cursor_show(
                (int)mouse_now.x,
                (int)mouse_now.y
            );
        }
    }

    continue;
}



    if (device != PS2_INPUT_KEYBOARD) {
        continue;
    }

    /* Prefijo de tecla extendida */
    

    /* Prefijo de tecla extendida */
    if (code == 0xE0U) {
        desktop_extended = 1;
        continue;
    }

    /* Ignorar liberacion de teclas */
    if (code & 0x80U) {
        desktop_extended = 0;
        continue;
    }

   

    if (desktop_extended) {
        desktop_extended = 0;

        /* Flecha arriba */
        if (code == 0x48U) {
            if (desktop_window > 0U) {
                desktop_window--;
                redraw = 1;
            }
        }

        /* Flecha abajo */
        else if (code == 0x50U) {
            if (desktop_window < 4U) {
                desktop_window++;
                redraw = 1;
            }
        }

    } else {
        /* Teclas numericas 1 a 4 */
        if (code >= 0x02U &&
            code <= 0x05U) {

            desktop_window = code - 1U;
            redraw = 1;
        }

        /* Tecla 0: Inicio */
        else if (code == 0x0BU) {
            desktop_window = 0U;
            redraw = 1;
        }

        /* ESC: Inicio */
        else if (code == 0x01U) {
            desktop_window = 0U;
            redraw = 1;
        }

        /* Enter: actualizar seleccion */
        else if (code == 0x1CU) {
            redraw = 1;
        }
    }

    if (redraw) {
        if (desktop_render_app(desktop_window) != 0) {
            serial_write("ERROR: cambio de ventana.\n");
        }
    }
}

#endif

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
    /*
     * Actualizar la interfaz aunque
     * el usuario no pulse ninguna tecla.
     */
    if (gui_active()) {
        gui_update();
    }

    
if (!(inb(0x64) & 1)) {
    continue;
}

uint8_t codigo = inb(0x60);


        if (ignorar_pausa) { ignorar_pausa--; continue; }
        if (codigo == 0xE1) { ignorar_pausa = 5; continue; }
      
if (codigo == 0xE0) {
    extendido = 1;
    continue;
}

/*
 * Teclas extendidas PS/2.
 * Permitir las flechas dentro de la GUI.
 */
if (extendido) {
    extendido = 0;

    /* Ignorar eventos de liberacion */
    if ((codigo & 0x80U) != 0U) {
        continue;
    }

    if (gui_active()) {
        if (codigo == 0x48 ||
            codigo == 0x50 ||
            codigo == 0x4B ||
            codigo == 0x4D) {

            gui_key(codigo);
        }
    }

    /* No enviar teclas extendidas a consola */
    continue;
}

        
/* Control del escritorio MiOS */
if (gui_active()) {

    /* ESC: regresar a la consola */
    if (codigo == 0x01) {
        gui_close();

        longitud = 0;
        color = 0x0F;
        limpiar_pantalla();

        imprimir(
            "MiOS - Modo consola\n"
        );

        mostrar_prompt();
    } else if ((codigo & 0x80U) == 0U) {
        gui_key(codigo);
    }

    continue;
}


        if (codigo == 0x2A || codigo == 0x36) { shift = 1; continue; }
        if (codigo == 0xAA || codigo == 0xB6) { shift = 0; continue; }
        if (codigo == 0x3A) { caps = !caps; continue; }
        if (codigo & 0x80) continue;

        if (codigo == 0x1C) {
            entrada[longitud] = '\0';
            poner_caracter('\n');
            ejecutar_comando(entrada);
longitud = 0;

if (!gui_active()) {
    mostrar_prompt();
}
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

static void desktop_draw_terminal(void)
{
    /* Sombra */
    fb_draw_rect(
        194, 128, 403, 277,
        9, 24, 43
    );

    /* Marco de la ventana */
    fb_draw_rect(
        190, 124, 403, 277,
        205, 219, 231
    );

    /* Barra del titulo */
    fb_draw_rect(
        190, 124, 403, 28,
        24, 106, 156
    );

    fb_text_draw(
        204, 134, "MiOS TERMINAL", 1,
        255, 255, 255
    );

    /* Boton de cerrar (visual) */
    fb_draw_rect(
        564, 129, 22, 18,
        179, 61, 68
    );

    fb_text_draw(
        571, 134, "X", 1,
        255, 255, 255
    );

    /* Fondo de la consola */
    fb_draw_rect(
        196, 156, 391, 239,
        16, 23, 35
    );

    fb_text_draw(
        211, 172, "MiOS Terminal", 1,
        125, 234, 177
    );

    fb_text_draw(
        211, 191, "Kernel x86 32-bit", 1,
        190, 205, 222
    );

    fb_text_draw(
        211, 225, "mios> _", 1,
        130, 255, 160
    );
}


/* MiOS Desktop 0.6.1 - Barra de tareas */

/* MiOS Desktop - Fondo moderno */

static void desktop_draw_wallpaper(void)
{
    /* Degradado azul oscuro */
    for (int y = 0; y < 480; y++) {
        int shade = y / 20;

        fb_draw_rect(
            0, y, 640, 1,
            (uint8_t)(11 + shade),
            (uint8_t)(29 + shade),
            (uint8_t)(53 + shade)
        );
    }

    /* Detalles decorativos */
    fb_draw_rect(178, 78, 425, 3, 28, 112, 153);
    fb_draw_rect(178, 83, 310, 2, 27, 79, 113);

    fb_text_draw(
        196, 96, "MiOS DESKTOP", 2,
        237, 247, 255
    );

    fb_text_draw(
        196, 119, "TU SISTEMA OPERATIVO", 1,
        144, 198, 224
    );
}


static void desktop_draw_taskbar(uint32_t window)
{
    static const char *menu[5] = {
        "INICIO", "PROCESOS", "MEMORIA",
        "ARCHIVOS", "ACERCA"
    };

    static const char *dock_labels[4] = {
        "INI", "TERM", "ARCH", "SIST"
    };

    static const char *dock_icons[4] = {
        "M", ">", "F", "S"
    };

    static const uint32_t dock_apps[4] = {
        0, 5, 3, 4
    };

    const char *title = "ESCRITORIO";

    if (window == 1) title = "PROCESOS";
    if (window == 2) title = "MEMORIA";
    if (window == 3) title = "ARCHIVOS";
    if (window == 4) title = "SISTEMA";
    if (window == 5) title = "TERMINAL";
    /* Ocultar el menu lateral antiguo */
for (int y = 45; y < 424; y++) {
    int shade = y / 20;

    fb_draw_rect(
        0, y, 175, 1,
        (uint8_t)(11 + shade),
        (uint8_t)(29 + shade),
        (uint8_t)(53 + shade)
    );
}

    /* Barra superior */
    fb_draw_rect(0, 0, 640, 45, 10, 23, 40);
    fb_draw_rect(0, 43, 640, 2, 39, 154, 190);

    fb_text_draw(
        20, 14, "MiOS", 2,
        255, 255, 255
    );

    fb_text_draw(
        264, 18, title, 1,
        175, 220, 241
    );



    /* Barra inferior */
    fb_draw_rect(
        0, 424, 640, 56,
        9, 20, 35
    );

    /* Sombra y cuerpo del dock */
    fb_draw_rect(
        183, 427, 275, 51,
        5, 13, 25
    );

    fb_draw_rect(
        186, 429, 270, 47,
        30, 51, 73
    );

    /* Cuatro aplicaciones */
    for (int i = 0; i < 4; i++) {
        int x = 199 + i * 64;

        if (window == dock_apps[i]) {
            fb_draw_rect(
                x, 433, 48, 37,
                31, 143, 191
            );
        } else {
            fb_draw_rect(
                x, 433, 48, 37,
                43, 73, 105
            );
        }

        fb_text_draw(
            x + 18, 438,
            dock_icons[i], 2,
            255, 255, 255
        );

        fb_text_draw(
            x + 9, 457,
            dock_labels[i], 1,
            224, 239, 249
        );
    }
}


static int desktop_render_app(uint32_t window)
{
    int result;

    /* Terminal */
    if (window == MIOS_APP_TERMINAL) {

        result = fb_desktop_draw_live(0);

        if (result != 0)
            return result;

        desktop_draw_wallpaper();
        desktop_draw_taskbar(window);

        desktop_draw_terminal();
        term_draw_content();

        return 0;
    }

    /* Ventanas del sistema */
    result = fb_desktop_draw_live(window);

    if (result != 0)
        return result;

    /* Escritorio principal */
    if (window == 0) {
        desktop_draw_wallpaper();
        desktop_draw_app_icons();
    }

    /* Barras y menú modernos */
    desktop_draw_taskbar(window);

    return 0;
}



