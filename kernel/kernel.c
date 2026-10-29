#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "interrupts.h"
#include "memory.h"
#include "thread.h"
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
        imprimir("Error: no se pudieron crear hilos.\n");
    } else {
        imprimir("Hilos creados: ");
        imprimir_numero(thread_count());
        imprimir("\n\n");

        if (thread_run() == 0) {
            imprimir("\nAmbos hilos finalizaron.\n");
        } else {
            imprimir("Error al ejecutar hilos.\n");
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

void kernel_main(void) {
    mios_fs_init();
    memory_init();
    thread_init();
    color = 0x0F;
    limpiar_pantalla();
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
