
#include <stdint.h>

#include "binfs.h"
#include "ata.h"
#include "irq.h"

#define BINFS_CATALOG_LBA 512U
#define BINFS_DATA_LBA 513U

#define BINFS_SECTOR_SIZE 512U
#define BINFS_SECTORS_PER_FILE 16U

#define BINFS_MAX_FILES 4U
#define BINFS_ENTRY_SIZE 64U
#define BINFS_FIRST_ENTRY 16U

static uint8_t catalog[BINFS_SECTOR_SIZE];
static uint8_t sector[BINFS_SECTOR_SIZE];
/*
 * Buffer de 8192 bytes para copiar
 * un archivo completo dentro de BINFS.
 */
static uint8_t copy_buffer[BINFS_MAX_FILE_SIZE];

static const uint8_t magic[8] = {
    'M', 'I', 'O', 'S', 'B', 'I', 'N', '1'
};

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t checksum32(
    const uint8_t *data,
    uint32_t size
) {
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }

    return hash;
}

/*
 * Comparar un nombre del comando run
 * con un nombre almacenado en 32 bytes.
 *
 * El nombre debe terminar en cero.
 */
static int names_equal(
    const char *requested,
    const uint8_t *stored
) {
    if (requested == 0)
        return 0;

    for (uint32_t i = 0; i < 32U; i++) {
        char a = requested[i];
        char b = (char)stored[i];

        if (a != b)
            return 0;

        if (a == '\0')
            return 1;
    }

    return 0;
}

int binfs_read(
    const char *filename,
    uint8_t *buffer,
    uint32_t capacity,
    uint32_t *bytes_read
) {
    if (filename == 0 ||
        buffer == 0 ||
        bytes_read == 0) {
        return -1;
    }

    *bytes_read = 0;

    uint32_t flags = irq_save();

    if (ata_read_sector(
            BINFS_CATALOG_LBA,
            catalog
        ) != 0) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t i = 0; i < 8U; i++) {
        if (catalog[i] != magic[i]) {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t count = read_u32(&catalog[8]);

    if (count == 0U || count > BINFS_MAX_FILES) {
        irq_restore(flags);
        return -1;
    }

    for (uint32_t index = 0;
         index < count;
         index++) {

        uint32_t offset =
            BINFS_FIRST_ENTRY +
            index * BINFS_ENTRY_SIZE;

        const uint8_t *entry =
            &catalog[offset];

        if (!names_equal(filename, entry))
            continue;

        uint32_t lba = read_u32(entry + 32U);
        uint32_t size = read_u32(entry + 36U);
        uint32_t expected = read_u32(entry + 40U);

        /*
         * En esta version cada posicion
         * tiene una region fija exclusiva.
         *
         * No se permite que el catalogo
         * redirija una lectura fuera de ella.
         */
        uint32_t permitted_lba =
            BINFS_DATA_LBA +
            index * BINFS_SECTORS_PER_FILE;

        if (lba != permitted_lba ||
            size == 0U ||
            size > BINFS_MAX_FILE_SIZE ||
            size > capacity) {

            irq_restore(flags);
            return -1;
        }

        uint32_t sectors =
            (size + BINFS_SECTOR_SIZE - 1U) /
            BINFS_SECTOR_SIZE;

        if (sectors > BINFS_SECTORS_PER_FILE) {
            irq_restore(flags);
            return -1;
        }

        for (uint32_t s = 0; s < sectors; s++) {
            if (ata_read_sector(
                    lba + s,
                    sector
                ) != 0) {

                irq_restore(flags);
                return -1;
            }

            uint32_t position =
                s * BINFS_SECTOR_SIZE;

            uint32_t remaining =
                size - position;

            uint32_t count_bytes =
                remaining < BINFS_SECTOR_SIZE
                    ? remaining
                    : BINFS_SECTOR_SIZE;

            for (uint32_t j = 0;
                 j < count_bytes;
                 j++) {

                buffer[position + j] = sector[j];
            }
        }

        if (checksum32(buffer, size) != expected) {
            irq_restore(flags);
            return -1;
        }

        *bytes_read = size;

        irq_restore(flags);
        return 0;
    }

    irq_restore(flags);
    return -1;
}

/*
 * Validar y copiar una entrada del
 * catalogo BINFS experimental.
 */
static int binfs_decode_entry(
    uint32_t index,
    BinfsEntry *result
) {
    if (result == 0 ||
        index >= BINFS_MAX_FILES) {
        return -1;
    }

    uint32_t offset =
        BINFS_FIRST_ENTRY +
        index * BINFS_ENTRY_SIZE;

    const uint8_t *entry = &catalog[offset];

    /* Exigir terminacion NUL del nombre */
    int terminated = 0;

    for (uint32_t i = 0; i < 32U; i++) {
        if (entry[i] == 0U) {
            terminated = 1;
            break;
        }
    }

    if (!terminated || entry[0] == 0U)
        return -1;

    uint32_t lba = read_u32(entry + 32U);
    uint32_t size = read_u32(entry + 36U);

    uint32_t permitted_lba =
        BINFS_DATA_LBA +
        index * BINFS_SECTORS_PER_FILE;

    if (lba != permitted_lba ||
        size == 0U ||
        size > BINFS_MAX_FILE_SIZE) {
        return -1;
    }

    for (uint32_t i = 0; i < 32U; i++) {
        result->name[i] = (char)entry[i];
    }

    result->name[31] = '\0';
    result->size = size;
    result->lba = lba;

    return 0;
}

/*
 * Leer y comprobar la cabecera
 * del catalogo BINFS.
 */
static int binfs_load_catalog(void) {
    if (ata_read_sector(
            BINFS_CATALOG_LBA,
            catalog
        ) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < 8U; i++) {
        if (catalog[i] != magic[i])
            return -1;
    }

    uint32_t count = read_u32(&catalog[8]);

    if (count == 0U ||
        count > BINFS_MAX_FILES) {
        return -1;
    }

    return (int)count;
}

int binfs_count(void) {
    uint32_t flags = irq_save();

    int count = binfs_load_catalog();

    if (count > 0) {
        for (int i = 0; i < count; i++) {
            BinfsEntry entry;

            if (binfs_decode_entry(
                    (uint32_t)i,
                    &entry
                ) != 0) {
                count = -1;
                break;
            }
        }
    }

    irq_restore(flags);
    return count;
}

int binfs_get_entry(
    uint32_t index,
    BinfsEntry *result
) {
    if (result == 0)
        return -1;

    uint32_t flags = irq_save();

    int count = binfs_load_catalog();

    if (count < 0 ||
        index >= (uint32_t)count) {
        irq_restore(flags);
        return -1;
    }

    int status =
        binfs_decode_entry(index, result);

    irq_restore(flags);
    return status;
}

int binfs_copy_file(
    const char *source,
    const char *destination
) {
    if (source == 0 || destination == 0)
        return -1;

    /*
     * Comprobar el nombre destino.
     * Maximo: 31 caracteres y NUL.
     * No permitir rutas ni espacios.
     */
    uint32_t name_length = 0;

    while (name_length < 32U &&
           destination[name_length] != '\0') {

        char c = destination[name_length];

        if (c == '/' || c == '\\' ||
            c == ' ' || c == '\t' ||
            (uint8_t)c < 33U ||
            (uint8_t)c > 126U) {
            return -1;
        }

        name_length++;
    }

    if (name_length == 0U ||
        name_length >= 32U)
        return -1;

    /*
     * Primero leer el archivo origen.
     * binfs_read verifica su checksum.
     */
    uint32_t size = 0;

    if (binfs_read(
            source,
            copy_buffer,
            sizeof(copy_buffer),
            &size
        ) != 0) {
        return -1;
    }

    if (size == 0U ||
        size > BINFS_MAX_FILE_SIZE)
        return -1;

    uint32_t flags = irq_save();

    /*
     * Volver a leer el catalogo para
     * trabajar con su estado actual.
     */
    int count = binfs_load_catalog();

    if (count <= 0 ||
        count >= (int)BINFS_MAX_FILES) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Rechazar nombres repetidos.
     */
    for (int i = 0; i < count; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (names_equal(
                destination,
                &catalog[offset]
            )) {
            irq_restore(flags);
            return -1;
        }
    }

    /*
     * La siguiente posicion del catalogo
     * sera ocupada por el archivo nuevo.
     */
    uint32_t index = (uint32_t)count;

    uint32_t destination_lba =
        BINFS_DATA_LBA +
        index * BINFS_SECTORS_PER_FILE;

    /*
     * No sobrescribir sectores que
     * ya contengan informacion.
     */
    for (uint32_t s = 0;
         s < BINFS_SECTORS_PER_FILE;
         s++) {

        if (ata_read_sector(
                destination_lba + s,
                sector
            ) != 0) {
            irq_restore(flags);
            return -1;
        }

        for (uint32_t j = 0;
             j < BINFS_SECTOR_SIZE;
             j++) {

            if (sector[j] != 0U) {
                irq_restore(flags);
                return -1;
            }
        }
    }

    /*
     * Copiar los datos, incluyendo los
     * ceros que forman parte del ELF.
     */
    for (uint32_t s = 0;
         s < BINFS_SECTORS_PER_FILE;
         s++) {

        uint32_t offset =
            s * BINFS_SECTOR_SIZE;

        for (uint32_t j = 0;
             j < BINFS_SECTOR_SIZE;
             j++) {

            uint32_t position = offset + j;

            sector[j] =
                position < size
                    ? copy_buffer[position]
                    : 0U;
        }

        if (ata_write_sector(
                destination_lba + s,
                sector
            ) != 0) {

            irq_restore(flags);
            return -1;
        }
    }

    /*
     * Verificar que cada byte haya sido
     * escrito correctamente en el disco.
     */
    for (uint32_t s = 0;
         s < BINFS_SECTORS_PER_FILE;
         s++) {

        if (ata_read_sector(
                destination_lba + s,
                sector
            ) != 0) {

            irq_restore(flags);
            return -1;
        }

        uint32_t offset =
            s * BINFS_SECTOR_SIZE;

        for (uint32_t j = 0;
             j < BINFS_SECTOR_SIZE;
             j++) {

            uint32_t position = offset + j;

            uint8_t expected_byte =
                position < size
                    ? copy_buffer[position]
                    : 0U;

            if (sector[j] != expected_byte) {
                irq_restore(flags);
                return -1;
            }
        }
    }

    /*
     * Los datos ya estan escritos
     * y verificados.
     *
     * Registrar la nueva entrada.
     */
    uint32_t entry_offset =
        BINFS_FIRST_ENTRY +
        index * BINFS_ENTRY_SIZE;

    for (uint32_t i = 0;
         i < BINFS_ENTRY_SIZE;
         i++) {

        catalog[entry_offset + i] = 0;
    }

    for (uint32_t i = 0;
         i < name_length;
         i++) {

        catalog[entry_offset + i] =
            (uint8_t)destination[i];
    }

    /* LBA inicial */
    catalog[entry_offset + 32U] =
        (uint8_t)destination_lba;

    catalog[entry_offset + 33U] =
        (uint8_t)(destination_lba >> 8);

    catalog[entry_offset + 34U] =
        (uint8_t)(destination_lba >> 16);

    catalog[entry_offset + 35U] =
        (uint8_t)(destination_lba >> 24);

    /* Tamaño exacto */
    catalog[entry_offset + 36U] =
        (uint8_t)size;

    catalog[entry_offset + 37U] =
        (uint8_t)(size >> 8);

    catalog[entry_offset + 38U] =
        (uint8_t)(size >> 16);

    catalog[entry_offset + 39U] =
        (uint8_t)(size >> 24);

    /* Checksum del contenido */
    uint32_t hash = checksum32(
        copy_buffer,
        size
    );

    catalog[entry_offset + 40U] =
        (uint8_t)hash;

    catalog[entry_offset + 41U] =
        (uint8_t)(hash >> 8);

    catalog[entry_offset + 42U] =
        (uint8_t)(hash >> 16);

    catalog[entry_offset + 43U] =
        (uint8_t)(hash >> 24);

    /*
     * Actualizar el numero de archivos.
     * El resto de los bytes 8-11 se
     * conserva en cero.
     */
    uint32_t new_count = index + 1U;

    catalog[8] = (uint8_t)new_count;
    catalog[9] = 0;
    catalog[10] = 0;
    catalog[11] = 0;

    /*
     * Publicar el catalogo al final,
     * despues de verificar los datos.
     */
    if (ata_write_sector(
            BINFS_CATALOG_LBA,
            catalog
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Verificar lectura del catalogo */
    if (binfs_load_catalog() != (int)new_count) {
        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}
