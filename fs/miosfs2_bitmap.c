
#include <stdint.h>

#include "miosfs2_bitmap.h"
#include "irq.h"

/*
 * Un bit por bloque:
 *
 * 0 = libre
 * 1 = ocupado
 *
 * 1024 bloques requieren 128 bytes.
 */
#define BITMAP_BYTES \
    ((MIOSFS2_BLOCK_COUNT + 7U) / 8U)

static uint8_t bitmap[BITMAP_BYTES];
static uint32_t free_blocks = 0;
static int initialized = 0;

static uint8_t block_mask(uint32_t block) {
    return (uint8_t)(1U << (block % 8U));
}

static int block_is_used(uint32_t block) {
    uint32_t byte_index = block / 8U;

    return (
        bitmap[byte_index] & block_mask(block)
    ) != 0U;
}

void miosfs2_bitmap_init(void) {
    uint32_t flags = irq_save();

    for (uint32_t i = 0; i < BITMAP_BYTES; i++) {
        bitmap[i] = 0;
    }

    free_blocks = MIOSFS2_BLOCK_COUNT;
    initialized = 1;

    irq_restore(flags);
}

int miosfs2_block_alloc(uint32_t *block) {
    if (block == 0)
        return -1;

    uint32_t flags = irq_save();

    if (!initialized || free_blocks == 0U) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0;
         i < MIOSFS2_BLOCK_COUNT;
         i++) {

        if (!block_is_used(i)) {
            uint32_t byte_index = i / 8U;

            bitmap[byte_index] |= block_mask(i);

            free_blocks--;
            *block = i;

            irq_restore(flags);
            return 0;
        }
    }

    irq_restore(flags);
    return -1;
}

int miosfs2_block_free(uint32_t block) {
    if (block >= MIOSFS2_BLOCK_COUNT)
        return -1;

    uint32_t flags = irq_save();

    if (!initialized || !block_is_used(block)) {
        irq_restore(flags);
        return -1;
    }

    uint32_t byte_index = block / 8U;

    bitmap[byte_index] &=
        (uint8_t)~block_mask(block);

    free_blocks++;

    irq_restore(flags);
    return 0;
}

int miosfs2_block_used(uint32_t block) {
    if (block >= MIOSFS2_BLOCK_COUNT)
        return -1;

    uint32_t flags = irq_save();

    if (!initialized) {
        irq_restore(flags);
        return -1;
    }

    int result = block_is_used(block);

    irq_restore(flags);
    return result;
}

uint32_t miosfs2_blocks_free(void) {
    uint32_t flags = irq_save();

    uint32_t result =
        initialized ? free_blocks : 0U;

    irq_restore(flags);
    return result;
}

/*
 * Prueba del administrador en RAM.
 *
 * Debe ejecutarse antes de montar
 * un sistema de archivos real, porque
 * reinicia el bitmap experimental.
 */
int miosfs2_bitmap_selftest(void) {
    miosfs2_bitmap_init();

    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;

    int ok = 1;

    /* Reservar tres bloques */
    if (miosfs2_block_alloc(&a) != 0 ||
        miosfs2_block_alloc(&b) != 0 ||
        miosfs2_block_alloc(&c) != 0) {
        ok = 0;
    }

    if (ok) {
        if (a != 0U || b != 1U || c != 2U)
            ok = 0;

        if (miosfs2_blocks_free() !=
            MIOSFS2_BLOCK_COUNT - 3U)
            ok = 0;

        if (miosfs2_block_used(a) != 1 ||
            miosfs2_block_used(b) != 1 ||
            miosfs2_block_used(c) != 1)
            ok = 0;
    }

    /* Liberar y comprobar rechazo de doble liberacion */
    if (ok) {
        if (miosfs2_block_free(b) != 0)
            ok = 0;

        if (miosfs2_block_free(b) == 0)
            ok = 0;
    }

    /* El primer bloque libre debe ser b */
    if (ok) {
        uint32_t reused = UINT32_MAX;

        if (miosfs2_block_alloc(&reused) != 0 ||
            reused != b) {
            ok = 0;
        }
    }

    /* Comprobar rechazo de bloque fuera de rango */
    if (miosfs2_block_free(MIOSFS2_BLOCK_COUNT)
        == 0) {
        ok = 0;
    }

    /* Reiniciar el bitmap experimental */
    miosfs2_bitmap_init();

    if (miosfs2_blocks_free() !=
        MIOSFS2_BLOCK_COUNT) {
        ok = 0;
    }

    return ok ? 0 : -1;
}
