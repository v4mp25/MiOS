
#include <stdint.h>
#include "framebuffer.h"

/* Firma Multiboot 1 al entrar al kernel */
#define MULTIBOOT_BOOT_MAGIC 0x2BADB002U

/* Bit 12: informacion de framebuffer */
#define MULTIBOOT_FRAMEBUFFER_FLAG (1U << 12)

static MiosFramebufferInfo fb_info;

/* Leer un entero de 32 bits little-endian */
static uint32_t read32(
    const volatile uint8_t *p
) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void framebuffer_probe(
    uint32_t multiboot_magic,
    uint32_t multiboot_info
) {
    fb_info = (MiosFramebufferInfo){0};

    if (multiboot_magic != MULTIBOOT_BOOT_MAGIC)
        return;

    if (multiboot_info == 0U)
        return;

    /*
     * La estructura Multiboot debe ser
     * accesible al iniciar el kernel.
     *
     * El sondeo se llama antes de
     * activar la paginacion.
     */
    const volatile uint8_t *mb =
        (const volatile uint8_t *)
        (uintptr_t)multiboot_info;

    uint32_t flags = read32(mb);

    if (!(flags & MULTIBOOT_FRAMEBUFFER_FLAG))
        return;

    /*
     * Multiboot 1:
     * offset 88: direccion framebuffer, u64
     * offset 96: pitch, u32
     * offset 100: ancho, u32
     * offset 104: alto, u32
     * offset 108: profundidad, u8
     * offset 109: tipo, u8
     */
    fb_info.address_low  = read32(mb + 88);
    fb_info.address_high = read32(mb + 92);

    fb_info.pitch  = read32(mb + 96);
    fb_info.width  = read32(mb + 100);
    fb_info.height = read32(mb + 104);
    fb_info.bpp    = mb[108];
    fb_info.type   = mb[109];

    if (fb_info.width == 0U ||
        fb_info.height == 0U ||
        fb_info.pitch == 0U ||
        fb_info.bpp == 0U) {
        return;
    }

    fb_info.available = 1U;
}

const MiosFramebufferInfo *
framebuffer_get_info(void) {
    return &fb_info;
}
