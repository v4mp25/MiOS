
#ifndef MIOSFS2_DISK_H
#define MIOSFS2_DISK_H

#include <stdint.h>

/*
 * Operaciones experimentales de MIOSFS02
 * sobre un disco de laboratorio.
 */

/* Leer y validar superbloque y bitmap */
int miosfs2_disk_check(void);

/* Consultar el estado persistente del bloque 0 */
int miosfs2_disk_block0(int *used);

/*
 * Cambiar y guardar el estado del bloque 0.
 *
 * used = 1: ocupado
 * used = 0: libre
 *
 * Se rechazan operaciones duplicadas.
 */
int miosfs2_disk_set_block0(int used);

#endif
