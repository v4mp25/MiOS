
#ifndef MIOS_BINFS_H
#define MIOS_BINFS_H

#include <stdint.h>

#define BINFS_MAX_FILE_SIZE 8192U

/*
 * Leer un archivo binario por nombre.
 * Devuelve el tamano real en bytes_read.
 *
 * Es una interfaz experimental de lectura.
 */
int binfs_read(
    const char *filename,
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *bytes_read
);

/*
 * Informacion de un archivo BINFS.
 * El nombre dispone de 32 bytes.
 */
typedef struct {
    char name[32];
    uint32_t size;
    uint32_t lba;
} BinfsEntry;

/*
 * Obtener el numero de archivos
 * registrados en el catalogo.
 *
 * Devuelve -1 si el catalogo es invalido.
 */
int binfs_count(void);

/*
 * Consultar una entrada del catalogo.
 *
 * index comienza en cero.
 * 0 = exito, -1 = error.
 */
int binfs_get_entry(
    uint32_t index,
    BinfsEntry *result
);
/*
 * Copiar un archivo BINFS.
 *
 * Reutiliza primero una posicion
 * eliminada; si no existe, intenta
 * crear una nueva.
 *
 * No sobrescribe archivos activos.
 */
int binfs_copy_file(
    const char *source,
    const char *destination
);

/*
 * Cambiar el nombre de un archivo BINFS.
 *
 * No cambia su tamaño, LBA ni checksum.
 *
 * 0 = exito
 * -1 = error
 */
int binfs_rename_file(
    const char *old_name,
    const char *new_name
);

/*
 * Eliminar logicamente el ultimo archivo
 * del catalogo BINFS experimental.
 *
 * No elimina los bytes fisicos.
 *
 * 0 = exito
 * -1 = error
 */
int binfs_remove_file(const char *filename);

#endif
