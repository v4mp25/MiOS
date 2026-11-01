
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

/*
 * Lee el catalogo valido mas reciente.
 *
 * destination debe disponer de 512 bytes.
 *
 * Devuelve el numero de slots (1 a 4),
 * o -1 si no existe una copia valida.
 *
 * Esta funcion NO escribe al disco.
 */
int binfs_catalog_read_latest(
    uint8_t *destination
);

#endif
