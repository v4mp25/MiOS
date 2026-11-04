
#ifndef MIOS_FB_DRAW_H
#define MIOS_FB_DRAW_H

#include <stdint.h>

/*
 * Dibujo directo sobre el framebuffer
 * ya mapeado y con paginacion activa.
 */

/* Verificar condiciones de dibujo */
int fb_draw_ready(void);

/* Pintar pantalla completa */
int fb_draw_clear(
    uint8_t r,
    uint8_t g,
    uint8_t b
);

/* Dibujar rectangulo con recorte */
int fb_draw_rect(
    int x,
    int y,
    int width,
    int height,
    uint8_t r,
    uint8_t g,
    uint8_t b
);

/* Primera demostracion grafica */
int fb_draw_demo(void);

#endif
