
#ifndef MIOS_BINFS_MIRROR_H
#define MIOS_BINFS_MIRROR_H

#include <stdint.h>

#define BINFS_PRIMARY_CATALOG_LBA 512U
#define BINFS_MIRROR_CATALOG_LBA  600U

/*
 * Diagnostico inicial del catalogo duplicado.
 *
 * Solo lectura.
 *
 * 0  = ambos catalogos son iguales
 * -1 = error, firma invalida o diferencia
 */
int binfs_mirror_verify(void);

#endif
