
#include <stdint.h>
#include "gui.h"

#define GUI_W 80
#define GUI_H 25

#define COLOR_FONDO   0x1F
#define COLOR_BARRA   0x1E
#define COLOR_PANEL   0x70
#define COLOR_TEXTO   0x0F
#define COLOR_PIE     0x1E
#define COLOR_MENU    0x17

static volatile uint16_t *const video =
    (volatile uint16_t *)0xB8000;

static int activa = 0;
static int ventana = 0;

/* Escribir un caracter con color */
static void pixel_texto(
    int x, int y, char c, uint8_t color
) {
    if (x < 0 || x >= GUI_W ||
        y < 0 || y >= GUI_H) {
        return;
    }

    video[y * GUI_W + x] =
        ((uint16_t)color << 8) |
        (uint8_t)c;
}

/* Escribir una cadena */
static void texto(
    int x, int y,
    const char *cadena,
    uint8_t color
) {
    while (*cadena && x < GUI_W) {
        pixel_texto(x, y, *cadena, color);
        x++;
        cadena++;
    }
}

/* Dibujar un rectangulo */
static void rectangulo(
    int x, int y,
    int ancho, int alto,
    uint8_t color
) {
    for (int fila = y;
         fila < y + alto;
         fila++) {

        for (int col = x;
             col < x + ancho;
             col++) {

            pixel_texto(
                col, fila, ' ', color
            );
        }
    }
}

/* Dibujar una ventana con marco */
static void marco(
    int x, int y,
    int ancho, int alto
) {
    rectangulo(
        x, y,
        ancho, alto,
        COLOR_PANEL
    );

    for (int i = 0; i < ancho; i++) {
        pixel_texto(
            x + i, y,
            '-', COLOR_PANEL
        );

        pixel_texto(
            x + i, y + alto - 1,
            '-', COLOR_PANEL
        );
    }

    for (int i = 0; i < alto; i++) {
        pixel_texto(
            x, y + i,
            '|', COLOR_PANEL
        );

        pixel_texto(
            x + ancho - 1, y + i,
            '|', COLOR_PANEL
        );
    }

    pixel_texto(x, y, '+', COLOR_PANEL);
    pixel_texto(
        x + ancho - 1, y,
        '+', COLOR_PANEL
    );

    pixel_texto(
        x, y + alto - 1,
        '+', COLOR_PANEL
    );

    pixel_texto(
        x + ancho - 1,
        y + alto - 1,
        '+', COLOR_PANEL
    );
}

/* Dibujar el escritorio completo */
static void dibujar(void) {
    rectangulo(
        0, 0, GUI_W, GUI_H,
        COLOR_FONDO
    );

    /* Barra superior */
    rectangulo(
        0, 0, GUI_W, 2,
        COLOR_BARRA
    );

    texto(
        3, 0,
        "MiOS DESKTOP 0.1",
        COLOR_BARRA
    );

    texto(
        56, 0,
        "Kernel x86 32-bit",
        COLOR_BARRA
    );

    /* Menu lateral */
    rectangulo(
        2, 4, 23, 17,
        COLOR_MENU
    );

    texto(4, 5, "MENU PRINCIPAL", COLOR_MENU);
    texto(4, 8, "1 - Procesos", COLOR_MENU);
    texto(4, 10, "2 - Memoria", COLOR_MENU);
    texto(4, 12, "3 - Archivos", COLOR_MENU);
    texto(4, 14, "4 - Acerca de", COLOR_MENU);
    texto(4, 17, "0 - Inicio", COLOR_MENU);

    /* Ventana principal */
    marco(28, 4, 49, 17);

    if (ventana == 0) {
        texto(
            32, 6,
            "BIENVENIDO A MiOS",
            COLOR_PANEL
        );

        texto(
            32, 9,
            "Sistema operativo experimental",
            COLOR_PANEL
        );

        texto(
            32, 11,
            "Kernel propio en C y ASM",
            COLOR_PANEL
        );

        texto(
            32, 13,
            "Selecciona una opcion",
            COLOR_PANEL
        );

        texto(
            32, 15,
            "desde el menu izquierdo.",
            COLOR_PANEL
        );

    } else if (ventana == 1) {
        texto(
            32, 6,
            "ADMINISTRADOR DE PROCESOS",
            COLOR_PANEL
        );

        texto(
            32, 9,
            "Procesos con PID y estados",
            COLOR_PANEL
        );

        texto(
            32, 11,
            "Planificador Round Robin",
            COLOR_PANEL
        );

        texto(
            32, 13,
            "Comandos de consola:",
            COLOR_PANEL
        );

        texto(
            32, 15,
            "proctest / ps / schedtest",
            COLOR_PANEL
        );

    } else if (ventana == 2) {
        texto(
            32, 6,
            "ADMINISTRADOR DE MEMORIA",
            COLOR_PANEL
        );

        texto(
            32, 9,
            "Memoria fisica y virtual",
            COLOR_PANEL
        );

        texto(
            32, 11,
            "Paginas de 4096 bytes",
            COLOR_PANEL
        );

        texto(
            32, 13,
            "Comandos de consola:",
            COLOR_PANEL
        );

        texto(
            32, 15,
            "pagingon / pmmvalidate",
            COLOR_PANEL
        );

    } else if (ventana == 3) {
        texto(
            32, 6,
            "SISTEMA DE ARCHIVOS",
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
            32, 13,
            "Comandos de consola:",
            COLOR_PANEL
        );

        texto(
            32, 15,
            "ls / binls / fs2diskcheck",
            COLOR_PANEL
        );

    } else if (ventana == 4) {
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
            "Arquitectura x86",
            COLOR_PANEL
        );

        texto(
            32, 13,
            "Kernel propio",
            COLOR_PANEL
        );

        texto(
            32, 15,
            "Interfaz VGA experimental",
            COLOR_PANEL
        );
    }

    /* Barra inferior */
    rectangulo(
        0, 23, GUI_W, 2,
        COLOR_PIE
    );

    texto(
        3, 23,
        "MiOS Desktop | 1-4: ventanas | 0: inicio",
        COLOR_PIE
    );

    texto(
        60, 23,
        "ESC: Consola",
        COLOR_PIE
    );
}

void gui_open(void) {
    activa = 1;
    ventana = 0;
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

    dibujar();
}
