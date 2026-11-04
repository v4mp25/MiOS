
#ifndef MIOS_GRAPHICS_H
#define MIOS_GRAPHICS_H

#include <stdint.h>

/*
 * Motor grafico experimental.
 *
 * Superficie de prueba en RAM.
 * No escribe todavia en memoria de video.
 */
#define GFX_WIDTH 64U
#define GFX_HEIGHT 48U

#define GFX_RGB(r, g, b) \
    (0xFF000000U | \
    ((uint32_t)(r) << 16) | \
    ((uint32_t)(g) << 8) | \
    (uint32_t)(b))

void gfx_clear(uint32_t color);

void gfx_pixel(
    int x,
    int y,
    uint32_t color
);

void gfx_rect(
    int x,
    int y,
    int width,
    int height,
    uint32_t color
);

uint32_t gfx_get_pixel(int x, int y);

int gfx_selftest(void);

#endif
