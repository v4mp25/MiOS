
#ifndef MIOS_BINFS_CONSISTENCY_H
#define MIOS_BINFS_CONSISTENCY_H

#include <stdint.h>

typedef struct {
    uint32_t catalog_valid;
    uint32_t generation;

    uint32_t files_checked;
    uint32_t files_ok;
    uint32_t files_damaged;

    uint32_t data_consistent;
} BinfsCopyReport;

typedef struct {
    BinfsCopyReport a;
    BinfsCopyReport b;

    uint32_t selected_lba;
    uint32_t selected_generation;
    uint32_t selected_data_ok;
} BinfsConsistencyReport;

/*
 * Verificar catalogos y regiones de datos.
 *
 * No escribe sectores.
 *
 * 0  = diagnostico completado
 * -1 = no se pudo completar
 */
int binfs_consistency_check(
    BinfsConsistencyReport *result
);

/*
 * Diagnostico de recuperacion BINFS.
 *
 * No modifica el disco.
 */
typedef struct {
    uint32_t active_lba;
    uint32_t active_generation;

    uint32_t active_data_ok;
    uint32_t fallback_data_ok;

    uint32_t recoverable_catalogs;
    uint32_t full_fallback_available;
} BinfsRecoveryReport;

int binfs_recovery_check(
    BinfsRecoveryReport *result
);

/*
 * Leer el catalogo mas reciente cuyos
 * metadatos Y datos sean integros.
 *
 * destination debe tener 512 bytes.
 *
 * Devuelve numero de slots o -1.
 * Solo lectura.
 */
int binfs_catalog_read_recoverable(
    uint8_t *destination
);

/*
 * Comprobar si es seguro iniciar una
 * modificacion del almacenamiento.
 *
 * Devuelve 1 si ambas versiones son
 * coherentes; 0 en caso contrario.
 */
int binfs_recovery_write_allowed(void);

#endif
