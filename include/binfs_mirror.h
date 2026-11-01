
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

/* Diagnostico de catalogos con generacion */
typedef struct {
    uint32_t primary_generation;
    uint32_t mirror_generation;
    uint32_t selected_generation;

    uint32_t primary_valid;
    uint32_t mirror_valid;
    uint32_t selected_lba;
} BinfsGenerationStatus;

/*
 * Valida checksums y selecciona
 * el catalogo valido mas reciente.
 *
 * Solo lectura. No repara ni escribe.
 */
int binfs_generation_check(
    BinfsGenerationStatus *result
);

#endif
