
#ifndef MIOS_FRAMEBUFFER_H
#define MIOS_FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
    uint32_t available;

    uint32_t address_low;
    uint32_t address_high;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t type;

} MiosFramebufferInfo;

/*
 * Consultar datos entregados por Multiboot 1.
 *
 * No modifica el modo de video.
 * No escribe en el framebuffer.
 */
void framebuffer_probe(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
);

/* Obtener resultado del sondeo */
const MiosFramebufferInfo *
framebuffer_get_info(void);

#endif

