
#ifndef MIOS_FB_TEXT_H
#define MIOS_FB_TEXT_H

#include <stdint.h>

/*
 * Motor de texto grafico 5x7.
 *
 * Dibuja letras sobre el framebuffer
 * utilizando las funciones existentes
 * de fb_draw.c.
 *
 * scale:
 * 1 = caracter de 5x7 pixeles
 * 2 = caracter de 10x14 pixeles
 * 3 = caracter de 15x21 pixeles
 */
int fb_text_draw(
    int x,
    int y,
    const char *text,
    int scale,
    uint8_t r,
    uint8_t g,
    uint8_t b
);

#endif
