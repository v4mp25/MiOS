
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

#endif
