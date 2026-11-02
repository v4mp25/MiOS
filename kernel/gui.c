
#include <stdint.h>

#include "gui.h"
#include "process.h"
#include "pmm.h"
#include "interrupts.h"

#define GUI_W 80
#define GUI_H 25

#define COLOR_FONDO 0x1F
#define COLOR_BARRA 0x1E
#define COLOR_PANEL 0x70
#define COLOR_MENU  0x17
#define COLOR_PIE   0x1E

#define GUI_REFRESH_TICKS 25U
#define GUI_TIMER_HZ 100U

static volatile uint16_t *const video =
    (volatile uint16_t *)0xB8000;

static int activa = 0;
static int ventana = 0;

static uint32_t ultimo_tick = 0;

/* Escribir caracter VGA */
static void caracter(
    int x,
    int y,
    char c,
    uint8_t color
) {
    if (x < 0 || x >= GUI_W ||
        y < 0 || y >= GUI_H) {
        return;
    }

    video[y * GUI_W + x] =
        ((uint16_t)color << 8) |
        (uint8_t)c;
}

/* Escribir texto */
static void texto(
    int x,
    int y,
    const char *cadena,
    uint8_t color
) {
    while (*cadena && x < GUI_W) {
        caracter(x, y, *cadena, color);
        x++;
        cadena++;
    }
}

/* Convertir entero decimal */
static void numero(
    int x,
    int y,
    uint32_t valor,
    uint8_t color
) {
    char buffer[11];
    int i = 0;

    if (valor == 0U) {
        caracter(x, y, '0', color);
        return;
    }

    while (valor > 0U && i < 10) {
        buffer[i++] =
            (char)('0' + valor % 10U);

        valor /= 10U;
    }

    for (int j = i - 1; j >= 0; j--) {
        caracter(x++, y, buffer[j], color);
    }
}

/* Rellenar zona rectangular */
static void rectangulo(
    int x,
    int y,
    int ancho,
    int alto,
    uint8_t color
) {
    for (int fila = y;
         fila < y + alto;
         fila++) {

        for (int col = x;
             col < x + ancho;
             col++) {

            caracter(
                col,
                fila,
                ' ',
                color
            );
        }
    }
}

/* Dibujar marco */
static void marco(
    int x,
    int y,
    int ancho,
    int alto
) {
    rectangulo(
        x,
        y,
        ancho,
        alto,
        COLOR_PANEL
    );

    for (int i = 0; i < ancho; i++) {
        caracter(
            x + i, y,
            '-', COLOR_PANEL
        );

        caracter(
            x + i, y + alto - 1,
            '-', COLOR_PANEL
        );
    }

    for (int i = 0; i < alto; i++) {
        caracter(
            x, y + i,
            '|', COLOR_PANEL
        );

        caracter(
            x + ancho - 1, y + i,
            '|', COLOR_PANEL
        );
    }

    caracter(x, y, '+', COLOR_PANEL);
    caracter(
        x + ancho - 1,
        y,
        '+',
        COLOR_PANEL
    );

    caracter(
        x,
        y + alto - 1,
        '+',
        COLOR_PANEL
    );

    caracter(
        x + ancho - 1,
        y + alto - 1,
        '+',
        COLOR_PANEL
    );
}

/* Convertir estado de proceso a texto */
static const char *estado_proceso(
    ProcessState estado
) {
    switch (estado) {
        case PROCESS_FREE:
            return "LIBRE";

        case PROCESS_READY:
            return "LISTO";

        case PROCESS_RUNNING:
            return "EJECUTANDO";

        case PROCESS_TERMINATED:
            return "TERMINADO";

        default:
            return "DESCONOCIDO";
    }
}

/* Ventana de procesos */
static void dibujar_procesos(void) {
    texto(
        31, 6,
        "MONITOR DE PROCESOS",
        COLOR_PANEL
    );

    texto(
        31, 8,
        "PID  NOMBRE              ESTADO",
        COLOR_PANEL
    );

    uint32_t encontrados = 0;

    for (uint32_t i = 0;
         i < MIOS_MAX_PROCESSES;
         i++) {

        ProcessInfo info;

        if (process_get_info(i, &info) != 0)
            continue;

        if (info.state == PROCESS_FREE)
            continue;

        int y = 10 + (int)i * 2;

        numero(
            31, y,
            info.pid,
            COLOR_PANEL
        );

        texto(
            36, y,
            info.name,
            COLOR_PANEL
        );

        texto(
            57, y,
            estado_proceso(info.state),
            COLOR_PANEL
        );

        encontrados++;
    }

    if (encontrados == 0U) {
        texto(
            31, 11,
            "No hay procesos registrados.",
            COLOR_PANEL
        );
    }

    texto(
        31, 16,
        "Procesos registrados:",
        COLOR_PANEL
    );

    numero(
        53, 16,
        encontrados,
        COLOR_PANEL
    );

    texto(
        31, 18,
        "Consola: proctest / ps",
        COLOR_PANEL
    );
}

/* Dibujar barra porcentual */
static void barra_memoria(
    int x,
    int y,
    uint32_t porcentaje
) {
    const uint32_t ancho = 34U;

    if (porcentaje > 100U)
        porcentaje = 100U;

    uint32_t ocupados =
        (porcentaje * ancho) / 100U;

    for (uint32_t i = 0; i < ancho; i++) {
        caracter(
            x + (int)i,
            y,
            i < ocupados ? '#' : '-',
            COLOR_PANEL
        );
    }
}

/* Ventana de memoria */
static void dibujar_memoria(void) {
    uint32_t total = pmm_total_frames();
    uint32_t libres = pmm_free_frames();
    uint32_t usados = pmm_used_frames();

    texto(
        31, 6,
        "MONITOR DE MEMORIA FISICA",
        COLOR_PANEL
    );

    texto(
        31, 9,
        "Marcos totales:",
        COLOR_PANEL
    );
    numero(50, 9, total, COLOR_PANEL);

    texto(
        31, 11,
        "Marcos libres:",
        COLOR_PANEL
    );
    numero(50, 11, libres, COLOR_PANEL);

    texto(
        31, 13,
        "Marcos usados:",
        COLOR_PANEL
    );
    numero(50, 13, usados, COLOR_PANEL);

    uint32_t porcentaje = 0U;

    if (total > 0U) {
        porcentaje = (usados * 100U) / total;
    }

    texto(
        31, 15,
        "Uso:",
        COLOR_PANEL
    );

    numero(
        37, 15,
        porcentaje,
        COLOR_PANEL
    );

    texto(
        40, 15,
        "%",
        COLOR_PANEL
    );

    barra_memoria(
        31, 17,
        porcentaje
    );

    texto(
        31, 19,
        "Cada marco: 4096 bytes",
        COLOR_PANEL
    );
}

/* Pantalla inicial */
static void dibujar_inicio(void) {
    texto(
        32, 6,
        "BIENVENIDO A MiOS",
        COLOR_PANEL
    );

    texto(
        32, 9,
        "Desktop 0.2",
        COLOR_PANEL
    );

    texto(
        32, 11,
        "Kernel x86 de 32 bits",
        COLOR_PANEL
    );

    texto(
        32, 13,
        "Monitor de procesos y RAM",
        COLOR_PANEL
    );

    texto(
        32, 15,
        "Selecciona una ventana.",
        COLOR_PANEL
    );
}

/* Ventana de archivos */
static void dibujar_archivos(void) {
    texto(
        32, 6,
        "SISTEMAS DE ARCHIVOS",
        COLOR_PANEL
    );

    texto(
        32, 9,
        "MIOSFS01 / BINFS",
        COLOR_PANEL
    );

    texto(
        32, 11,
        "MIOSFS02 en desarrollo",
        COLOR_PANEL
    );

    texto(
        32, 14,
        "Comandos:",
        COLOR_PANEL
    );

    texto(
        32, 16,
        "ls / binls / fs2diskcheck",
        COLOR_PANEL
    );
}

/* Informacion */
static void dibujar_acerca(void) {
    texto(
        32, 6,
        "ACERCA DE MiOS",
        COLOR_PANEL
    );

    texto(
        32, 9,
        "Sistema operativo academico",
        COLOR_PANEL
    );

    texto(
        32, 11,
        "C y ensamblador x86",
        COLOR_PANEL
    );

    texto(
        32, 13,
        "Kernel propio",
        COLOR_PANEL
    );

    texto(
        32, 15,
        "Monitor VGA experimental",
        COLOR_PANEL
    );
}

/* Redibujar pantalla */
static void dibujar(void) {
    rectangulo(
        0, 0,
        GUI_W, GUI_H,
        COLOR_FONDO
    );

    rectangulo(
        0, 0,
        GUI_W, 2,
        COLOR_BARRA
    );

    texto(
        3, 0,
        "MiOS DESKTOP 0.2",
        COLOR_BARRA
    );

    texto(
        53, 0,
        "Tiempo:",
        COLOR_BARRA
    );

    numero(
        61, 0,
        timer_get_ticks() / GUI_TIMER_HZ,
        COLOR_BARRA
    );

    texto(
        72, 0,
        "seg",
        COLOR_BARRA
    );

    rectangulo(
        2, 4,
        23, 17,
        COLOR_MENU
    );

    texto(
        4, 5,
        "MENU PRINCIPAL",
        COLOR_MENU
    );

    texto(4, 8,  "1 - Procesos", COLOR_MENU);
    texto(4, 10, "2 - Memoria", COLOR_MENU);
    texto(4, 12, "3 - Archivos", COLOR_MENU);
    texto(4, 14, "4 - Acerca de", COLOR_MENU);
    texto(4, 17, "0 - Inicio", COLOR_MENU);

    marco(28, 4, 49, 17);

    if (ventana == 0)
        dibujar_inicio();
    else if (ventana == 1)
        dibujar_procesos();
    else if (ventana == 2)
        dibujar_memoria();
    else if (ventana == 3)
        dibujar_archivos();
    else if (ventana == 4)
        dibujar_acerca();

    rectangulo(
        0, 23,
        GUI_W, 2,
        COLOR_PIE
    );

    texto(
        3, 23,
        "1-4: Ventanas | 0: Inicio",
        COLOR_PIE
    );

    texto(
        59, 23,
        "ESC: Consola",
        COLOR_PIE
    );
}

void gui_open(void) {
    activa = 1;
    ventana = 0;
    ultimo_tick = timer_get_ticks();

    dibujar();
}

void gui_close(void) {
    activa = 0;
}

int gui_active(void) {
    return activa;
}

void gui_key(uint8_t scancode) {
    if (!activa)
        return;

    if (scancode == 0x0B) {
        ventana = 0;
    } else if (scancode >= 0x02 &&
               scancode <= 0x05) {
        ventana = scancode - 1;
    } else {
        return;
    }

    ultimo_tick = timer_get_ticks();
    dibujar();
}

/*
 * Actualizar como maximo cada 25 ticks.
 * PIT configurado a 100 Hz.
 *
 * Ejecutar desde el bucle del kernel.
 */
void gui_update(void) {
    if (!activa)
        return;

    uint32_t ahora = timer_get_ticks();

    if ((uint32_t)(ahora - ultimo_tick) <
        GUI_REFRESH_TICKS) {
        return;
    }

    ultimo_tick = ahora;
    dibujar();
}
