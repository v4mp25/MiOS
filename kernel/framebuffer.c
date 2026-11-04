
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

/*
 * Comprobar que los campos RGB
 * representan tres canales de 8 bits
 * sin solapamiento.
 */
static int valid_rgb_fields(
    const volatile uint8_t *p
) {
    uint32_t rp = p[0];
    uint32_t rs = p[1];

    uint32_t gp = p[2];
    uint32_t gs = p[3];

    uint32_t bp = p[4];
    uint32_t bs = p[5];

    if (rs != 8U || gs != 8U || bs != 8U)
        return 0;

    if (rp > 24U || gp > 24U || bp > 24U)
        return 0;

    uint32_t rm = 0xFFU << rp;
    uint32_t gm = 0xFFU << gp;
    uint32_t bm = 0xFFU << bp;

    if ((rm & gm) || (rm & bm) || (gm & bm))
        return 0;

    return 1;
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
   
if (fb_info.type == 1U) {
    /*
     * Multiboot 1 especifica color_info
     * desde el offset 110.
     *
     * Aceptar tambien el offset 112
     * cuando los campos en 110 no
     * representen un RGB888 valido.
     */
    uint32_t rgb_offset = 110U;

    if (!valid_rgb_fields(mb + rgb_offset)) {
        rgb_offset = 112U;
    }

    if (valid_rgb_fields(mb + rgb_offset)) {
        fb_info.red_position =
            mb[rgb_offset + 0U];

        fb_info.red_size =
            mb[rgb_offset + 1U];

        fb_info.green_position =
            mb[rgb_offset + 2U];

        fb_info.green_size =
            mb[rgb_offset + 3U];

        fb_info.blue_position =
            mb[rgb_offset + 4U];

        fb_info.blue_size =
            mb[rgb_offset + 5U];
    }
}


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
