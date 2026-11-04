
#include <stdint.h>

#include "graphics.h"

/*
 * Buffer de pixeles.
 *
 * 64 x 48 x 4 bytes = 12 KiB.
 *
 * Estructura estatica: no se reserva
 * en la pila del kernel.
 */
static uint32_t pixels[GFX_WIDTH * GFX_HEIGHT];

static int inside(int x, int y) {
    return x >= 0 &&
           y >= 0 &&
           (uint32_t)x < GFX_WIDTH &&
           (uint32_t)y < GFX_HEIGHT;
}

void gfx_clear(uint32_t color) {
    for (uint32_t i = 0;
         i < GFX_WIDTH * GFX_HEIGHT;
         i++) {
        pixels[i] = color;
    }
}

void gfx_pixel(
    int x,
    int y,
    uint32_t color
) {
    if (!inside(x, y))
        return;

    pixels[(uint32_t)y * GFX_WIDTH +
           (uint32_t)x] = color;
}

uint32_t gfx_get_pixel(int x, int y) {
    if (!inside(x, y))
        return 0U;

    return pixels[(uint32_t)y * GFX_WIDTH +
                  (uint32_t)x];
}

void gfx_rect(
    int x,
    int y,
    int width,
    int height,
    uint32_t color
) {
    if (width <= 0 || height <= 0)
        return;

    /*
     * Limitar las coordenadas al
     * area valida antes de dibujar.
     */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;

    int64_t end_x = (int64_t)x + width;
    int64_t end_y = (int64_t)y + height;

    int x1 = end_x > (int64_t)GFX_WIDTH
           ? (int)GFX_WIDTH
           : (int)end_x;

    int y1 = end_y > (int64_t)GFX_HEIGHT
           ? (int)GFX_HEIGHT
           : (int)end_y;

    if (x1 <= x0 || y1 <= y0)
        return;

    for (int row = y0; row < y1; row++) {
        for (int col = x0; col < x1; col++) {
            gfx_pixel(col, row, color);
        }
    }
}

/*
 * Prueba de dibujo en RAM:
 * fondo azul, barra, ventana y borde.
 */
int gfx_selftest(void) {
    const uint32_t blue =
        GFX_RGB(12, 38, 85);

    const uint32_t white =
        GFX_RGB(245, 245, 245);

    const uint32_t cyan =
        GFX_RGB(45, 190, 220);

    const uint32_t dark =
        GFX_RGB(20, 30, 45);

    gfx_clear(blue);

    gfx_rect(0, 0, 64, 6, dark);

    gfx_rect(10, 10, 44, 30, white);

    gfx_rect(10, 10, 44, 5, cyan);

    /* Un pixel fuera de rango no escribe. */
    gfx_pixel(-1, -1, dark);
    gfx_pixel(64, 48, dark);

    /* Comprobar colores en zonas distintas. */
    if (gfx_get_pixel(0, 0) != dark)
        return -1;

    if (gfx_get_pixel(5, 20) != blue)
        return -1;

    if (gfx_get_pixel(12, 12) != cyan)
        return -1;

    if (gfx_get_pixel(15, 25) != white)
        return -1;

    if (gfx_get_pixel(63, 47) != blue)
        return -1;

    if (gfx_get_pixel(-1, -1) != 0U)
        return -1;

    return 0;
}
