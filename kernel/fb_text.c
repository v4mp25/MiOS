
#include <stdint.h>
#include <stddef.h>

#include "fb_text.h"
#include "fb_draw.h"

/*
 * Fuente basica 5x7.
 *
 * Cada caracter contiene 7 filas.
 * Solo se utilizan los 5 bits bajos.
 *
 * El bit 4 representa el pixel izquierdo.
 */

typedef struct {
    char character;
    uint8_t rows[7];
} FontGlyph;

static const FontGlyph font[] = {
    {'A',{14,17,17,31,17,17,17}},
    {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}},
    {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}},
    {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,14}},
    {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}},
    {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}},
    {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}},
    {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}},
    {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}},
    {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}},
    {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},
    {'Z',{31,1,2,4,8,16,31}},

    {'0',{14,17,19,21,25,17,14}},
    {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}},
    {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}},
    {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}},

    {':',{0,4,4,0,4,4,0}},
    {'.',{0,0,0,0,0,12,12}},
    {'-',{0,0,0,31,0,0,0}},
    {'/',{1,2,2,4,8,8,16}},
    {'!',{4,4,4,4,4,0,4}},
    {'?',{14,17,1,2,4,0,4}},
    {'%',{17,2,4,8,16,17,0}},
};

#define FONT_COUNT \
    (sizeof(font) / sizeof(font[0]))

/* Buscar un caracter en la fuente */
static const uint8_t *get_glyph(char c) {
    /* Convertir minusculas en mayusculas */
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    for (uint32_t i = 0;
         i < FONT_COUNT;
         i++) {

        if (font[i].character == c) {
            return font[i].rows;
        }
    }

    /* Caracter desconocido: signo ? */
    for (uint32_t i = 0;
         i < FONT_COUNT;
         i++) {

        if (font[i].character == '?') {
            return font[i].rows;
        }
    }

    return NULL;
}

int fb_text_draw(
    int x,
    int y,
    const char *text,
    int scale,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    if (text == NULL ||
        scale < 1 ||
        scale > 3) {
        return -1;
    }

    int cursor_x = x;
    int cursor_y = y;

    for (uint32_t i = 0; text[i]; i++) {
        char c = text[i];

        if (c == '\n') {
            cursor_x = x;
            cursor_y += 8 * scale;
            continue;
        }

        if (c == ' ') {
            cursor_x += 6 * scale;
            continue;
        }

        const uint8_t *glyph = get_glyph(c);

        if (glyph == NULL)
            return -1;

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {

                uint8_t mask =
                    (uint8_t)(1U << (4 - col));

                if ((glyph[row] & mask) == 0U)
                    continue;

                if (fb_draw_rect(
                    cursor_x + col * scale,
                    cursor_y + row * scale,
                    scale,
                    scale,
                    r, g, b
                ) != 0) {
                    return -1;
                }
            }
        }

        /* 5 pixeles + 1 de separacion */
        cursor_x += 6 * scale;
    }

    return 0;
}
