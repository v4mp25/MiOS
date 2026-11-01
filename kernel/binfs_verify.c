
#include <stdint.h>
#include <stddef.h>

#include "binfs_verify.h"
#include "binfs.h"
#include "elf32.h"
#include "irq.h"

/*
 * Buffer temporal de verificacion.
 * No pertenece a un proceso Ring 3.
 */
static uint8_t verify_buffer[BINFS_MAX_FILE_SIZE];

/*
 * Comparar dos nombres terminados en NUL.
 */
static int names_same(
    const char *a,
    const char *b
) {
    if (a == NULL || b == NULL)
        return 0;

    for (uint32_t i = 0; i < 32U; i++) {
        if (a[i] != b[i])
            return 0;

        if (a[i] == '\0')
            return 1;
    }

    return 0;
}

int binfs_verify_all(BinfsVerifyResult *result) {
    if (result == NULL)
        return -1;

    result->files_checked = 0;
    result->elf_valid = 0;
    result->errors = 0;

    /*
     * Evitar que otra operacion BINFS
     * modifique el catalogo mientras
     * realizamos la comprobacion.
     */
    uint32_t flags = irq_save();

    int count = binfs_count();

    if (count < 0 || count > 4) {
        result->errors++;
        irq_restore(flags);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        BinfsEntry entry;

        if (binfs_get_entry(
                (uint32_t)i,
                &entry
            ) != 0) {

            result->errors++;
            continue;
        }

        result->files_checked++;

        /*
         * Comprobar nombres y regiones
         * contra los archivos anteriores.
         */
        int duplicate = 0;

        for (int j = 0; j < i; j++) {
            BinfsEntry previous;

            if (binfs_get_entry(
                    (uint32_t)j,
                    &previous
                ) != 0) {
                duplicate = 1;
                break;
            }

            if (names_same(
                    entry.name,
                    previous.name
                ) ||
                entry.lba == previous.lba) {

                duplicate = 1;
                break;
            }
        }

        if (duplicate) {
            result->errors++;
            continue;
        }

        uint32_t size = 0;

        /*
         * binfs_read comprueba el checksum
         * registrado en el catalogo.
         */
        if (binfs_read(
                entry.name,
                verify_buffer,
                sizeof(verify_buffer),
                &size
            ) != 0) {

            result->errors++;
            continue;
        }

        if (size != entry.size) {
            result->errors++;
            continue;
        }

        Elf32_LoadInfo info;

        if (elf32_validate_segments(
                verify_buffer,
                size,
                &info
            ) != 0) {

            result->errors++;
            continue;
        }

        result->elf_valid++;
    }

    int ok =
        result->errors == 0U &&
        result->files_checked == (uint32_t)count &&
        result->elf_valid == (uint32_t)count;

    irq_restore(flags);

    return ok ? 0 : -1;
}
