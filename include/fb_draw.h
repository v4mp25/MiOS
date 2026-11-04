
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
/*
 * Dibujar una ventana del escritorio.
 *
 * 0 = Inicio
 * 1 = Procesos
 * 2 = Memoria
 * 3 = Archivos
 * 4 = Acerca de
 */
int fb_desktop_draw(uint32_t window);
/*
 * Dibujar escritorio con consultas
 * reales al kernel.
 */
int fb_desktop_draw_live(uint32_t window);
void fb_draw_cursor(int x, int y);
uint32_t fb_read_pixel(int x, int y);
void fb_write_pixel_raw(int x, int y, uint32_t pixel);
void fb_cursor_hide(void);
void fb_cursor_show(int x, int y);
#endif
