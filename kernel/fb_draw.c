
#include <stdint.h>
#include <stddef.h>

#include "fb_draw.h"
#include "framebuffer.h"
#include "paging.h"
#include "serial.h"
#include "fb_text.h"
#include "process.h"
#include "pmm.h"
#include "binfs.h"
static const MiosFramebufferInfo *fb = NULL;
static volatile uint8_t *fb_video = NULL;

static uint32_t pack_rgb(
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    return
        ((uint32_t)r << fb->red_position) |
        ((uint32_t)g << fb->green_position) |
        ((uint32_t)b << fb->blue_position);
}


int fb_draw_ready(void) {
    uint32_t cr3 = 0U;

    serial_write("FB DEBUG: iniciando validacion\n");

    if (!paging_cpu_enabled()) {
        serial_write("FB ERROR: paginacion inactiva\n");
        return 0;
    }

    if (!framebuffer_map_ready()) {
        serial_write("FB ERROR: mapeo no preparado\n");
        return 0;
    }

    __asm__ volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
    );

    uint32_t expected =
        paging_directory_address() & 0xFFFFF000U;

    uint32_t actual =
        cr3 & 0xFFFFF000U;

    serial_write("CR3 actual: ");
    serial_write_hex32(actual);
    serial_write("\n");

    serial_write("CR3 esperado: ");
    serial_write_hex32(expected);
    serial_write("\n");

    if (actual != expected) {
        serial_write("FB ERROR: directorio CR3 distinto\n");
        return 0;
    }

    fb = framebuffer_get_info();

    if (fb == NULL || !fb->available) {
        serial_write("FB ERROR: datos no disponibles\n");
        return 0;
    }

    serial_write("Ancho: ");
    serial_write_hex32(fb->width);
    serial_write("\n");

    serial_write("Alto: ");
    serial_write_hex32(fb->height);
    serial_write("\n");

    serial_write("Tipo: ");
    serial_write_hex32(fb->type);
    serial_write("\n");

    serial_write("BPP: ");
    serial_write_hex32(fb->bpp);
    serial_write("\n");

    if (fb->type != 1U ||
        fb->bpp != 32U ||
        fb->address_high != 0U ||
        fb->width != 640U ||
        fb->height != 480U) {

        serial_write("FB ERROR: formato incompatible\n");
        return 0;
    }

    serial_write("Rojo posicion: ");
    serial_write_hex32(fb->red_position);
    serial_write("\n");

    serial_write("Rojo bits: ");
    serial_write_hex32(fb->red_size);
    serial_write("\n");

    serial_write("Verde posicion: ");
    serial_write_hex32(fb->green_position);
    serial_write("\n");

    serial_write("Verde bits: ");
    serial_write_hex32(fb->green_size);
    serial_write("\n");

    serial_write("Azul posicion: ");
    serial_write_hex32(fb->blue_position);
    serial_write("\n");

    serial_write("Azul bits: ");
    serial_write_hex32(fb->blue_size);
    serial_write("\n");

    if (fb->red_size != 8U ||
        fb->green_size != 8U ||
        fb->blue_size != 8U) {

        serial_write("FB ERROR: tamanos RGB incompatibles\n");
        return 0;
    }

    if (fb->red_position > 24U ||
        fb->green_position > 24U ||
        fb->blue_position > 24U) {

        serial_write("FB ERROR: posiciones RGB invalidas\n");
        return 0;
    }

    uint32_t rm = 0xFFU << fb->red_position;
    uint32_t gm = 0xFFU << fb->green_position;
    uint32_t bm = 0xFFU << fb->blue_position;

    if ((rm & gm) ||
        (rm & bm) ||
        (gm & bm)) {

        serial_write("FB ERROR: canales RGB solapados\n");
        return 0;
    }

    serial_write("Pitch: ");
    serial_write_hex32(fb->pitch);
    serial_write("\n");

    serial_write("Bytes mapeados: ");
    serial_write_hex32(framebuffer_map_size());
    serial_write("\n");

    if (fb->pitch < 640U * 4U ||
        fb->pitch > 8192U) {

        serial_write("FB ERROR: pitch incompatible\n");
        return 0;
    }

    uint32_t bytes = fb->pitch * fb->height;

    if (bytes != framebuffer_map_size()) {
        serial_write("FB ERROR: tamano de mapeo diferente\n");
        return 0;
    }

    uint32_t address = framebuffer_map_address();

    serial_write("Direccion virtual: ");
    serial_write_hex32(address);
    serial_write("\n");

    if (address == 0U) {
        serial_write("FB ERROR: direccion virtual nula\n");
        return 0;
    }

    fb_video = (volatile uint8_t *)
        (uintptr_t)address;

    serial_write("FB DEBUG: TODAS LAS VALIDACIONES OK\n");

    return 1;
}


static void fb_put_pixel(
    uint32_t x,
    uint32_t y,
    uint32_t color
) {
    uint32_t offset =
        y * fb->pitch + x * 4U;

    /*
     * Escribir los cuatro bytes del pixel.
     * El formato RGB se empaqueto
     * usando las posiciones de Multiboot.
     */
    fb_video[offset + 0U] =
        (uint8_t)(color);

    fb_video[offset + 1U] =
        (uint8_t)(color >> 8);

    fb_video[offset + 2U] =
        (uint8_t)(color >> 16);

    fb_video[offset + 3U] =
        (uint8_t)(color >> 24);
}

int fb_draw_rect(
    int x,
    int y,
    int width,
    int height,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    if (fb_video == NULL ||
        fb == NULL ||
        width <= 0 ||
        height <= 0) {
        return -1;
    }

    int64_t left = x;
    int64_t top = y;
    int64_t right =
        (int64_t)x + width;
    int64_t bottom =
        (int64_t)y + height;

    if (left < 0) left = 0;
    if (top < 0) top = 0;

    if (right > (int64_t)fb->width)
        right = fb->width;

    if (bottom > (int64_t)fb->height)
        bottom = fb->height;

    if (right <= left || bottom <= top)
        return 0;

    uint32_t color = pack_rgb(r, g, b);

    for (uint32_t py = (uint32_t)top;
         py < (uint32_t)bottom;
         py++) {

        for (uint32_t px = (uint32_t)left;
             px < (uint32_t)right;
             px++) {

            fb_put_pixel(px, py, color);
        }
    }

    return 0;
}

int fb_draw_clear(
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    if (fb == NULL || fb_video == NULL)
        return -1;

    return fb_draw_rect(
        0, 0,
        (int)fb->width,
        (int)fb->height,
        r, g, b
    );
}


int fb_draw_demo(void) {
    if (!fb_draw_ready())
        return -1;

    /* Fondo del escritorio */
    if (fb_draw_clear(15, 37, 70) != 0)
        return -1;

    /* Barra superior */
    fb_draw_rect(0, 0, 640, 56, 0, 125, 173);

    fb_text_draw(
        20, 18, "MIOS DESKTOP",
        2, 255, 255, 255
    );

    fb_text_draw(
        350, 23, "KERNEL X86 32 BIT",
        1, 255, 255, 255
    );

    /* Panel lateral */
    fb_draw_rect(
        20, 78, 132, 360,
        31, 61, 98
    );

    fb_text_draw(
        48, 96, "MENU",
        2, 255, 255, 255
    );

    /* Inicio seleccionado */
    fb_draw_rect(
        30, 136, 112, 43,
        0, 125, 173
    );

    fb_text_draw(
        47, 150, "INICIO",
        2, 255, 255, 255
    );

    /* Otras opciones */
    fb_draw_rect(
        30, 190, 112, 43,
        40, 79, 120
    );

    fb_text_draw(
        42, 204, "PROCESOS",
        1, 255, 255, 255
    );

    fb_draw_rect(
        30, 244, 112, 43,
        40, 79, 120
    );

    fb_text_draw(
        45, 258, "MEMORIA",
        1, 255, 255, 255
    );

    fb_draw_rect(
        30, 298, 112, 43,
        40, 79, 120
    );

    fb_text_draw(
        42, 312, "ARCHIVOS",
        1, 255, 255, 255
    );

    fb_draw_rect(
        30, 352, 112, 43,
        40, 79, 120
    );

    fb_text_draw(
        48, 366, "ACERCA",
        1, 255, 255, 255
    );

    /* Sombra y ventana principal */
    fb_draw_rect(
        176, 84, 444, 354,
        5, 20, 40
    );

    fb_draw_rect(
        170, 78, 444, 354,
        236, 244, 252
    );

    /* Barra de titulo */
    fb_draw_rect(
        170, 78, 444, 54,
        34, 107, 180
    );

    fb_text_draw(
        190, 96, "BIENVENIDO A MIOS",
        2, 255, 255, 255
    );

    /* Informacion de inicio */
    fb_text_draw(
        198, 170, "SISTEMA OPERATIVO",
        2, 25, 55, 85
    );

    fb_text_draw(
        198, 218, "KERNEL X86 32 BIT",
        2, 25, 55, 85
    );

    fb_text_draw(
        198, 261, "FRAMEBUFFER RGB OK",
        2, 25, 55, 85
    );

    fb_text_draw(
        198, 304, "PAGINACION ACTIVA",
        2, 25, 55, 85
    );

    /* Elemento visual inferior */
    fb_draw_rect(
        480, 366, 112, 43,
        0, 125, 173
    );

    fb_text_draw(
        515, 379, "MIOS",
        2, 255, 255, 255
    );

    /* Barra inferior */
    fb_draw_rect(
        0, 456, 640, 24,
        21, 56, 88
    );

    fb_text_draw(
        20, 464, "MIOS DESKTOP 0.4D",
        1, 255, 255, 255
    );

    return 0;
}


/*
 * MiOS Desktop 0.4D.3
 *
 * Escritorio interactivo por teclado.
 * Redibuja la pantalla segun la ventana.
 */
int fb_desktop_draw(uint32_t window) {
    if (window > 4U)
        return -1;

    /* Dibujar el escritorio base */
    if (fb_draw_demo() != 0)
        return -1;

    static const char *menu[5] = {
        "INICIO",
        "PROCESOS",
        "MEMORIA",
        "ARCHIVOS",
        "ACERCA"
    };

    static const char *titles[5] = {
        "BIENVENIDO A MIOS",
        "ADMINISTRADOR DE PROCESOS",
        "ADMINISTRADOR DE MEMORIA",
        "EXPLORADOR DE ARCHIVOS",
        "ACERCA DE MIOS"
    };

    /* Actualizar todas las opciones del menu */
    for (uint32_t i = 0; i < 5U; i++) {
        int y = 136 + (int)i * 54;

        if (i == window) {
            fb_draw_rect(
                30, y, 112, 43,
                0, 125, 173
            );
        } else {
            fb_draw_rect(
                30, y, 112, 43,
                40, 79, 120
            );
        }

        fb_text_draw(
            39,
            y + 17,
            menu[i],
            1,
            255, 255, 255
        );
    }

    /* Limpiar el contenido anterior */
    fb_draw_rect(
        170, 132, 444, 300,
        236, 244, 252
    );

    /* Actualizar barra del titulo */
    fb_draw_rect(
        170, 78, 444, 54,
        34, 107, 180
    );

    fb_text_draw(
        190, 96,
        titles[window],
        2,
        255, 255, 255
    );

    if (window == 0U) {
        fb_text_draw(
            198, 175,
            "SISTEMA OPERATIVO",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 220,
            "KERNEL X86 32 BIT",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 265,
            "FRAMEBUFFER RGB OK",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 310,
            "PAGINACION ACTIVA",
            2,
            25, 55, 85
        );

    } else if (window == 1U) {
        fb_text_draw(
            198, 180,
            "GESTION DE PROCESOS",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 230,
            "PID Y ESTADOS",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 280,
            "PLANIFICADOR ROUND ROBIN",
            1,
            25, 55, 85
        );

    } else if (window == 2U) {
        fb_text_draw(
            198, 180,
            "MEMORIA FISICA Y VIRTUAL",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 230,
            "PAGINAS DE 4096 BYTES",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 280,
            "ADMINISTRADOR PMM",
            2,
            25, 55, 85
        );

    } else if (window == 3U) {
        fb_text_draw(
            198, 180,
            "SISTEMAS DE ARCHIVOS",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 230,
            "MIOSFS01 Y BINFS",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 280,
            "MIOSFS02 EN DESARROLLO",
            1,
            25, 55, 85
        );

    } else if (window == 4U) {
        fb_text_draw(
            198, 180,
            "MIOS DESKTOP 0.4D",
            2,
            25, 55, 85
        );

        fb_text_draw(
            198, 230,
            "KERNEL PROPIO EN C Y ASM",
            1,
            25, 55, 85
        );

        fb_text_draw(
            198, 280,
            "ARQUITECTURA X86",
            2,
            25, 55, 85
        );
    }

    return 0;
}

/*
 * Convertir enteros a texto decimal.
 * No necesita sprintf ni libc.
 */
static void fb_number(
    int x,
    int y,
    uint32_t value,
    int scale,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    char text[11];
    char reversed[10];
    uint32_t count = 0;

    do {
        reversed[count++] =
            (char)('0' + value % 10U);

        value /= 10U;
    } while (value && count < 10U);

    for (uint32_t i = 0; i < count; i++) {
        text[i] = reversed[count - 1U - i];
    }

    text[count] = '\0';

    fb_text_draw(
        x, y, text, scale, r, g, b
    );
}

static const char *fb_process_state(
    ProcessState state
) {
    switch (state) {
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

static void fb_monitor_processes(void) {
    const uint8_t r = 25;
    const uint8_t g = 55;
    const uint8_t b = 85;

    fb_text_draw(
        195, 155,
        "PID    NOMBRE          ESTADO",
        1, r, g, b
    );

    uint32_t count = 0U;

    for (uint32_t i = 0;
         i < MIOS_MAX_PROCESSES;
         i++) {

        ProcessInfo info;

        if (process_get_info(i, &info) != 0)
            continue;

        if (info.state == PROCESS_FREE)
            continue;

        int y = 190 + (int)i * 48;

        fb_number(
            195, y,
            info.pid, 2,
            r, g, b
        );

        fb_text_draw(
            246, y,
            info.name, 1,
            r, g, b
        );

        fb_text_draw(
            410, y,
            fb_process_state(info.state),
            1, r, g, b
        );

        count++;
    }

    if (count == 0U) {
        fb_text_draw(
            195, 210,
            "SIN PROCESOS REGISTRADOS",
            1, r, g, b
        );
    }

    fb_text_draw(
        195, 330,
        "TOTAL DE PROCESOS:",
        2, r, g, b
    );

    fb_number(
        440, 330,
        count, 2,
        r, g, b
    );
}

static void fb_monitor_memory(void) {
    const uint8_t r = 25;
    const uint8_t g = 55;
    const uint8_t b = 85;

    uint32_t total = pmm_total_frames();
    uint32_t free = pmm_free_frames();
    uint32_t used = pmm_used_frames();

    fb_text_draw(
        195, 160,
        "MARCOS TOTALES:",
        2, r, g, b
    );

    fb_number(
        425, 160, total,
        2, r, g, b
    );

    fb_text_draw(
        195, 207,
        "MARCOS LIBRES:",
        2, r, g, b
    );

    fb_number(
        410, 207, free,
        2, r, g, b
    );

    fb_text_draw(
        195, 254,
        "MARCOS USADOS:",
        2, r, g, b
    );

    fb_number(
        410, 254, used,
        2, r, g, b
    );

    /*
     * Evitar multiplicacion grande
     * antes de dividir.
     */
    uint32_t percent = 0U;

    if (total != 0U) {
        percent =
            (used / total) * 100U +
            ((used % total) * 100U) / total;
    }

    if (percent > 100U)
        percent = 100U;

    fb_text_draw(
        195, 310,
        "USO:",
        2, r, g, b
    );

    fb_number(
        258, 310, percent,
        2, r, g, b
    );

    fb_text_draw(
        294, 310,
        "%",
        2, r, g, b
    );

    /* Fondo de la barra */
    fb_draw_rect(
        195, 355, 340, 24,
        175, 194, 212
    );

    /* Barra de memoria ocupada */
    uint32_t width =
        (percent * 340U) / 100U;

    if (width > 0U) {
        fb_draw_rect(
            195, 355,
            (int)width, 24,
            0, 125, 173
        );
    }

    fb_text_draw(
        195, 395,
        "PAGINA: 4096 BYTES",
        1, r, g, b
    );
}

static void fb_monitor_files(void) {
    const uint8_t r = 25;
    const uint8_t g = 55;
    const uint8_t b = 85;

    fb_text_draw(
        195, 155,
        "ARCHIVOS BINFS",
        2, r, g, b
    );

    int count = binfs_count();

    if (count < 0) {
        fb_text_draw(
            195, 210,
            "CATALOGO NO DISPONIBLE",
            1, r, g, b
        );

        return;
    }

    if (count == 0) {
        fb_text_draw(
            195, 210,
            "NO HAY ARCHIVOS",
            1, r, g, b
        );

        return;
    }

    fb_text_draw(
        195, 205,
        "NOMBRE",
        1, r, g, b
    );

    fb_text_draw(
        425, 205,
        "BYTES",
        1, r, g, b
    );

    /* El prototipo BINFS admite
       hasta cuatro archivos. */
    if (count > 4)
        count = 4;

    for (int i = 0; i < count; i++) {
        BinfsEntry entry;

        if (binfs_get_entry(
                (uint32_t)i,
                &entry
            ) != 0) {
            fb_text_draw(
                195, 375,
                "ERROR LEYENDO ARCHIVOS",
                1, r, g, b
            );
            return;
        }

        int y = 235 + i * 36;

        fb_text_draw(
            195, y,
            entry.name,
            1, r, g, b
        );

        fb_number(
            425, y,
            entry.size,
            1, r, g, b
        );
    }
}

/*
 * Dibujar escritorio con informacion real.
 *
 * Primero redibuja el menu y la ventana,
 * despues incorpora los datos del kernel.
 */
int fb_desktop_draw_live(uint32_t window) {
    if (fb_desktop_draw(window) != 0)
        return -1;

    if (window == 1U) {
        /* Limpiar el texto decorativo */
        fb_draw_rect(
            170, 132, 444, 300,
            236, 244, 252
        );

        fb_monitor_processes();

    } else if (window == 2U) {
        fb_draw_rect(
            170, 132, 444, 300,
            236, 244, 252
        );

        fb_monitor_memory();

    } else if (window == 3U) {
        fb_draw_rect(
            170, 132, 444, 300,
            236, 244, 252
        );

        fb_monitor_files();
    }

    return 0;
}
