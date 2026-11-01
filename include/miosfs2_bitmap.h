
#ifndef MIOS_FS2_BITMAP_H
#define MIOS_FS2_BITMAP_H

#include <stdint.h>

/*
 * Administrador de bloques de MIOSFS02.
 *
 * Etapa 7.2: implementacion en RAM.
 * Todavia no escribe en disco.
 */

#define MIOSFS2_BLOCK_SIZE 512U
#define MIOSFS2_BLOCK_COUNT 1024U

/* Reiniciar todos los bloques como libres */
void miosfs2_bitmap_init(void);

/* Reservar el primer bloque libre */
int miosfs2_block_alloc(uint32_t *block);

/* Liberar un bloque ocupado */
int miosfs2_block_free(uint32_t block);

/* Consultar su estado: 0 libre, 1 ocupado */
int miosfs2_block_used(uint32_t block);

/* Consultar cantidad de bloques libres */
uint32_t miosfs2_blocks_free(void);

/* Comprobar asignacion y liberacion */
int miosfs2_bitmap_selftest(void);

#endif
