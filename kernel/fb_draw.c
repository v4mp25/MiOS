
#include <stdint.h>
#include <stddef.h>

#include "fb_draw.h"
#include "framebuffer.h"
#include "paging.h"
#include "serial.h"
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

    /* Fondo azul oscuro */
    if (fb_draw_clear(15, 37, 70) != 0)
        return -1;

    /* Barra superior */
    fb_draw_rect(
        0, 0, 640, 56,
        0, 125, 173
    );

    /* Sombra de la ventana */
    fb_draw_rect(
        116, 104, 420, 302,
        6, 20, 40
    );

    /* Ventana blanca */
    fb_draw_rect(
        108, 96, 420, 302,
        236, 244, 252
    );

    /* Barra de titulo */
    fb_draw_rect(
        108, 96, 420, 42,
        34, 107, 180
    );

    /* Elementos decorativos */
    fb_draw_rect(
        139, 166, 305, 19,
        181, 203, 226
    );

    fb_draw_rect(
        139, 201, 235, 19,
        181, 203, 226
    );

    fb_draw_rect(
        139, 236, 270, 19,
        181, 203, 226
    );

    /* Boton azul */
    fb_draw_rect(
        380, 320, 112, 42,
        0, 125, 173
    );

    return 0;
}
