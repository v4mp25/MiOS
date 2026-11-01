
#include <stdint.h>

#include "binfs.h"
#include "ata.h"
#include "irq.h"
#include "binfs_mirror.h"
static int binfs_load_catalog(void);
static uint32_t read_u32(const uint8_t *p);
static int binfs_region_available(
    uint32_t index,
    uint32_t target_lba
);
#define BINFS_CATALOG_LBA 512U
#define BINFS_DATA_LBA 513U

#define BINFS_SECTOR_SIZE 512U
#define BINFS_SECTORS_PER_FILE 16U
/* Segunda region de almacenamiento por slot */
#define BINFS_SHADOW_LBA 1024U

#define BINFS_MAX_FILES 4U
#define BINFS_ENTRY_SIZE 64U
#define BINFS_FIRST_ENTRY 16U

static uint8_t catalog[BINFS_SECTOR_SIZE];
static uint8_t sector[BINFS_SECTOR_SIZE];
/* Buffer exclusivo para actualizaciones */
static uint8_t replace_buffer[BINFS_MAX_FILE_SIZE];
static volatile int binfs_writes_paused = 1;

/* Catalogos para comprobar posiciones libres */
static uint8_t copy_catalog_a[512];
static uint8_t copy_catalog_b[512];
/*
 * Una posicion solo se considera
 * reutilizable cuando ambas copias
 * validas del catalogo la registran
 * como eliminada.
 *
 * BINFS utiliza entradas de 64 bytes.
 */
static int binfs_slot_safe_to_reuse(uint32_t index) {
    if (index >= BINFS_MAX_FILES)
        return 0;

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0)
        return 0;

    if (!status.primary_valid || !status.mirror_valid)
        return 0;

    if (ata_read_sector(
            BINFS_PRIMARY_CATALOG_LBA,
            copy_catalog_a
        ) != 0)
        return 0;

    if (ata_read_sector(
            BINFS_MIRROR_CATALOG_LBA,
            copy_catalog_b
        ) != 0)
        return 0;

    uint32_t slots_a = read_u32(&copy_catalog_a[8]);
    uint32_t slots_b = read_u32(&copy_catalog_b[8]);

    if (index >= slots_a || index >= slots_b)
        return 0;

    uint32_t offset =
        BINFS_FIRST_ENTRY +
        index * BINFS_ENTRY_SIZE;

    for (uint32_t i = 0; i < BINFS_ENTRY_SIZE; i++) {
        if (copy_catalog_a[offset + i] != 0U ||
            copy_catalog_b[offset + i] != 0U) {
            return 0;
        }
    }

    return 1;
}

/*
 * Buffer de 8192 bytes para copiar
 * un archivo completo dentro de BINFS.
 */
static uint8_t copy_buffer[BINFS_MAX_FILE_SIZE];



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

   
/*
 * Seleccionar el catalogo valido
 * mas reciente antes de buscar
 * el nombre del archivo.
 */
int count = binfs_load_catalog();

if (count < 1 ||
    count > (int)BINFS_MAX_FILES) {

    irq_restore(flags);
    return -1;
}


   for (uint32_t index = 0;
     index < (uint32_t)count;
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
            uint32_t shadow_lba =
    BINFS_SHADOW_LBA +
    index * BINFS_SECTORS_PER_FILE;

        if ((lba != permitted_lba &&
     lba != shadow_lba) ||
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
        uint32_t shadow_lba =
    BINFS_SHADOW_LBA +
    index * BINFS_SECTORS_PER_FILE;

    if ((lba != permitted_lba &&
     lba != shadow_lba)||
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
    /*
     * El nuevo lector comprueba ambas
     * copias BINFS v2 y selecciona
     * la generacion valida mas reciente.
     */
    return binfs_catalog_read_latest(catalog);
}




int binfs_count(void) {
    uint32_t flags = irq_save();

    int slots = binfs_load_catalog();

    if (slots < 0) {
        irq_restore(flags);
        return -1;
    }

    int active = 0;

    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        /*
         * Una entrada con nombre vacio
         * representa un slot eliminado.
         */
        if (catalog[offset] == 0U)
            continue;

        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i,
                &entry
            ) != 0) {
            irq_restore(flags);
            return -1;
        }

        active++;
    }

    irq_restore(flags);
    return active;
}





int binfs_get_entry(
    uint32_t index,
    BinfsEntry *result
) {
    if (result == 0)
        return -1;

    uint32_t flags = irq_save();

    int slots = binfs_load_catalog();

    if (slots < 0) {
        irq_restore(flags);
        return -1;
    }

    uint32_t visible_index = 0;

    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        /* Ignorar posiciones eliminadas */
        if (catalog[offset] == 0U)
            continue;

        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i,
                &entry
            ) != 0) {
            irq_restore(flags);
            return -1;
        }

        if (visible_index == index) {
            *result = entry;
            irq_restore(flags);
            return 0;
        }

        visible_index++;
    }

    irq_restore(flags);
    return -1;
}




/*
 * BINFS v2 - Copia segura a un tombstone.
 *
 * Requiere que ambas copias del catalogo
 * reconozcan el slot como eliminado.
 *
 * Los datos se verifican antes de
 * publicar una nueva generacion A/B.
 */
int binfs_copy_file(
    const char *source,
    const char *destination
) {
    if (source == 0 || destination == 0)
        return -1;

    /* Validar nombre destino */
    uint32_t name_length = 0;

    while (name_length < 32U &&
           destination[name_length] != '\0') {

        uint8_t c = (uint8_t)destination[name_length];

        if (c < 33U || c > 126U ||
            c == '/' || c == '\\') {
            return -1;
        }

        name_length++;
    }

    if (name_length == 0U || name_length >= 32U)
        return -1;

    uint32_t flags = irq_save();

    /* Leer origen y verificar checksum */
    uint32_t size = 0;

    if (binfs_read(
            source,
            copy_buffer,
            sizeof(copy_buffer),
            &size
        ) != 0 ||
        size == 0U ||
        size > BINFS_MAX_FILE_SIZE) {

        irq_restore(flags);
        return -1;
    }

    /* Leer catalogo v2 vigente */
    int slots = binfs_load_catalog();

    if (slots < 1 || slots > (int)BINFS_MAX_FILES) {
        irq_restore(flags);
        return -1;
    }

    uint32_t generation = read_u32(&catalog[480]);

    if (generation == 0U ||
        generation == UINT32_MAX) {
        irq_restore(flags);
        return -1;
    }

    int free_slot = -1;

    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (catalog[offset] == 0U) {
            if (free_slot < 0 &&
                binfs_slot_safe_to_reuse(
                    (uint32_t)i
                )) {
                free_slot = i;
            }
            continue;
        }

        /* Rechazar catalogo inconsistente */
        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i, &entry
            ) != 0) {
            irq_restore(flags);
            return -1;
        }

        /* Rechazar nombre repetido */
        if (names_equal(
                destination,
                &catalog[offset]
            )) {
            irq_restore(flags);
            return -1;
        }
    }

    /*
     * Por seguridad, esta primera
     * version A/B solo reutiliza
     * posiciones eliminadas.
     */
    if (free_slot < 0) {
        irq_restore(flags);
        return -1;
    }

    uint32_t index = (uint32_t)free_slot;

    uint32_t destination_lba =
        BINFS_DATA_LBA +
        index * BINFS_SECTORS_PER_FILE;

    /*
     * Escribir el nuevo contenido
     * en la region del slot eliminado.
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

    /* Leer de nuevo y verificar */
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

            uint8_t expected =
                position < size
                    ? copy_buffer[position]
                    : 0U;

            if (sector[j] != expected) {
                irq_restore(flags);
                return -1;
            }
        }
    }

    /* Preparar nueva entrada en RAM */
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

    uint32_t hash = checksum32(copy_buffer, size);

    /* LBA, tamaño y checksum */
    for (uint32_t i = 0; i < 4U; i++) {
        catalog[entry_offset + 32U + i] =
            (uint8_t)(destination_lba >> (8U * i));

        catalog[entry_offset + 36U + i] =
            (uint8_t)(size >> (8U * i));

        catalog[entry_offset + 40U + i] =
            (uint8_t)(hash >> (8U * i));
    }

    /*
     * Publicar la nueva entrada usando
     * el catalogo A/B y su generacion.
     */
    if (binfs_catalog_publish(
            catalog,
            generation
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Comprobar la nueva entrada visible */
    if (binfs_load_catalog() != slots) {
        irq_restore(flags);
        return -1;
    }

    BinfsEntry published;

    if (binfs_decode_entry(
            index,
            &published
        ) != 0 ||
        !names_equal(
            destination,
            (const uint8_t *)published.name
        ) ||
        published.lba != destination_lba ||
        published.size != size) {

        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}


/*
 * Renombrar un archivo BINFS v2.
 *
 * Publica los cambios mediante el
 * mecanismo redundante de catalogos A/B.
 *
 * No modifica los sectores del archivo.
 */
int binfs_rename_file(
    const char *old_name,
    const char *new_name
) {
    if (old_name == 0 || new_name == 0)
        return -1;

    uint32_t old_len = 0;
    uint32_t new_len = 0;

    /* Validar nombre anterior */
    while (old_len < 32U &&
           old_name[old_len] != '\0') {

        old_len++;
    }

    if (old_len == 0U || old_len >= 32U)
        return -1;

    /* Validar nombre nuevo */
    while (new_len < 32U &&
           new_name[new_len] != '\0') {

        uint8_t c = (uint8_t)new_name[new_len];

        if (c < 33U || c > 126U ||
            c == '/' || c == '\\') {
            return -1;
        }

        new_len++;
    }

    if (new_len == 0U || new_len >= 32U)
        return -1;

    uint32_t flags = irq_save();

    /*
     * Leer la copia valida mas reciente.
     */
    int slots = binfs_load_catalog();

    if (slots < 1 ||
        slots > (int)BINFS_MAX_FILES) {

        irq_restore(flags);
        return -1;
    }

    /*
     * La generacion actual se encuentra
     * en el byte 480 del catalogo v2.
     */
    uint32_t generation =
        (uint32_t)catalog[480] |
        ((uint32_t)catalog[481] << 8) |
        ((uint32_t)catalog[482] << 16) |
        ((uint32_t)catalog[483] << 24);

    if (generation == 0U ||
        generation == UINT32_MAX) {

        irq_restore(flags);
        return -1;
    }

    int source_index = -1;

    /*
     * Buscar el archivo anterior
     * y comprobar que el nombre nuevo
     * no exista.
     */
    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (catalog[offset] == 0U)
            continue;

        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i,
                &entry
            ) != 0) {

            irq_restore(flags);
            return -1;
        }

        if (names_equal(
                new_name,
                &catalog[offset]
            )) {

            irq_restore(flags);
            return -1;
        }

        if (names_equal(
                old_name,
                &catalog[offset]
            )) {

            source_index = i;
        }
    }

    if (source_index < 0) {
        irq_restore(flags);
        return -1;
    }

    uint32_t entry_offset =
        BINFS_FIRST_ENTRY +
        (uint32_t)source_index *
            BINFS_ENTRY_SIZE;

    /*
     * Modificar solamente los bytes
     * del nombre en nuestra copia RAM.
     *
     * El LBA, tamano y checksum
     * del archivo no se modifican.
     */
    for (uint32_t i = 0; i < 32U; i++) {
        catalog[entry_offset + i] = 0;
    }

    for (uint32_t i = 0; i < new_len; i++) {
        catalog[entry_offset + i] =
            (uint8_t)new_name[i];
    }

    /*
     * Publicar usando el mecanismo A/B.
     *
     * Esta funcion incrementa la
     * generacion, recalcula el checksum
     * y escribe el catalogo inactivo.
     */
    if (binfs_catalog_publish(
            catalog,
            generation
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Volver a leer el catalogo activo
     * despues de publicar.
     */
    int updated_slots = binfs_load_catalog();

    if (updated_slots != slots) {
        irq_restore(flags);
        return -1;
    }

    BinfsEntry updated_entry;

    if (binfs_decode_entry(
            (uint32_t)source_index,
            &updated_entry
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    if (!names_equal(
            new_name,
            (const uint8_t *)updated_entry.name
        )) {

        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}



/*
 * Eliminar logicamente cualquier archivo
 * BINFS, sin desplazar sus datos.
 *
 * El contador del catalogo pasa a
 * representar posiciones habilitadas,
 * que pueden contener tombstones.
 */

/*
 * Eliminar logicamente cualquier archivo
 * BINFS, sin desplazar sus datos.
 *
 * El contador del catalogo pasa a
 * representar posiciones habilitadas,
 * que pueden contener tombstones.
 */

/*
 * Eliminar logicamente cualquier archivo
 * BINFS, sin desplazar sus datos.
 *
 * El contador del catalogo pasa a
 * representar posiciones habilitadas,
 * que pueden contener tombstones.
 */

/*
 * Eliminar logicamente cualquier archivo
 * BINFS v2 mediante catalogos A/B.
 *
 * No modifica los sectores del contenido.
 * Publica un tombstone en una generacion
 * nueva del catalogo.
 */
int binfs_remove_file(const char *filename) {
    if (filename == 0) {
        return -1;
    }

    /* Validar nombre */
    uint32_t length = 0;

    while (length < 32U &&
           filename[length] != '\0') {
        length++;
    }

    if (length == 0U || length >= 32U) {
        return -1;
    }

    uint32_t flags = irq_save();

    /*
     * Cargar el catalogo BINFS v2
     * con la generacion mas reciente.
     */
    int slots = binfs_load_catalog();

    if (slots < 1 ||
        slots > (int)BINFS_MAX_FILES) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Obtener la generacion de la copia
     * que acabamos de cargar.
     *
     * Bytes 480-483: uint32 little endian.
     */
    uint32_t generation =
        (uint32_t)catalog[480] |
        ((uint32_t)catalog[481] << 8) |
        ((uint32_t)catalog[482] << 16) |
        ((uint32_t)catalog[483] << 24);

    if (generation == 0U ||
        generation == UINT32_MAX) {

        irq_restore(flags);
        return -1;
    }

    int found = -1;

    /*
     * Buscar el archivo solamente
     * entre las entradas activas.
     */
    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (catalog[offset] == 0U) {
            continue;
        }

        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i,
                &entry
            ) != 0) {

            irq_restore(flags);
            return -1;
        }

        if (names_equal(
                filename,
                &catalog[offset]
            )) {

            found = i;
            break;
        }
    }

    if (found < 0) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Eliminar la entrada en la copia RAM.
     *
     * El contenido fisico del archivo
     * permanece en su region A o B.
     */
    uint32_t entry_offset =
        BINFS_FIRST_ENTRY +
        (uint32_t)found * BINFS_ENTRY_SIZE;

    for (uint32_t i = 0;
         i < BINFS_ENTRY_SIZE;
         i++) {

        catalog[entry_offset + i] = 0;
    }

    /*
     * Conservar el numero de slots:
     * necesitamos recordar la posicion
     * para reutilizarla posteriormente.
     */

    /*
     * Publicar el tombstone en el
     * catalogo inactivo.
     *
     * La funcion incrementa generacion,
     * calcula checksum y verifica escritura.
     */
    if (binfs_catalog_publish(
            catalog,
            generation
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Volver a consultar la ultima copia
     * valida para confirmar la eliminacion.
     */
    int updated_slots = binfs_load_catalog();

    if (updated_slots != slots) {
        irq_restore(flags);
        return -1;
    }

    for (int i = 0; i < updated_slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (catalog[offset] == 0U) {
            continue;
        }

        if (names_equal(
                filename,
                &catalog[offset]
            )) {

            irq_restore(flags);
            return -1;
        }
    }

    irq_restore(flags);
    return 0;
}


/*
 * BINFS v2 - Reemplazar un ejecutable.
 *
 * Lee un archivo fuente, escribe sus
 * bytes en la region A/B disponible
 * del destino y publica el nuevo
 * catalogo mediante generacion/checksum.
 */
int binfs_replace_file(
    const char *source,
    const char *destination
) {
    if (source == 0 || destination == 0)
        return -1;

    /* Validar ambos nombres */
    const char *names[2] = {
        source, destination
    };

    for (uint32_t n = 0; n < 2U; n++) {
        uint32_t len = 0;

        while (len < 32U &&
               names[n][len] != '\0') {

            uint8_t c = (uint8_t)names[n][len];

            if (c < 33U || c > 126U ||
                c == '/' || c == '\\')
                return -1;

            len++;
        }

        if (len == 0U || len >= 32U)
            return -1;
    }

    /* Rechazar origen y destino iguales */
    int different = 0;

    for (uint32_t i = 0; i < 32U; i++) {
        if (source[i] != destination[i]) {
            different = 1;
            break;
        }

        if (source[i] == '\0')
            break;
    }

    if (!different)
        return -1;

    uint32_t flags = irq_save();

    /* Leer el origen y comprobar checksum */
    uint32_t size = 0;

    if (binfs_read(
            source,
            replace_buffer,
            sizeof(replace_buffer),
            &size
        ) != 0 ||
        size == 0U ||
        size > BINFS_MAX_FILE_SIZE) {

        irq_restore(flags);
        return -1;
    }

    /* Leer el catalogo v2 vigente */
    int slots = binfs_load_catalog();

    if (slots < 1 ||
        slots > (int)BINFS_MAX_FILES) {
        irq_restore(flags);
        return -1;
    }

    uint32_t generation =
        read_u32(&catalog[480]);

    if (generation == 0U ||
        generation == UINT32_MAX) {
        irq_restore(flags);
        return -1;
    }

    int source_index = -1;
    int target_index = -1;

    for (int i = 0; i < slots; i++) {
        uint32_t offset =
            BINFS_FIRST_ENTRY +
            (uint32_t)i * BINFS_ENTRY_SIZE;

        if (catalog[offset] == 0U)
            continue;

        BinfsEntry entry;

        if (binfs_decode_entry(
                (uint32_t)i,
                &entry
            ) != 0) {

            irq_restore(flags);
            return -1;
        }

        if (names_equal(
                source,
                &catalog[offset]
            )) {
            source_index = i;
        }

        if (names_equal(
                destination,
                &catalog[offset]
            )) {
            target_index = i;
        }
    }

    if (source_index < 0 ||
        target_index < 0 ||
        source_index == target_index) {

        irq_restore(flags);
        return -1;
    }

    uint32_t index =
        (uint32_t)target_index;

    uint32_t entry_offset =
        BINFS_FIRST_ENTRY +
        index * BINFS_ENTRY_SIZE;

    /* Dos regiones posibles del destino */
    uint32_t original_lba =
        BINFS_DATA_LBA +
        index * BINFS_SECTORS_PER_FILE;

    uint32_t shadow_lba =
        BINFS_SHADOW_LBA +
        index * BINFS_SECTORS_PER_FILE;

    uint32_t current_lba =
        read_u32(&catalog[entry_offset + 32U]);

    if (current_lba != original_lba &&
        current_lba != shadow_lba) {

        irq_restore(flags);
        return -1;
    }

    /* Seleccionar la region inactiva */
    uint32_t target_lba =
        current_lba == original_lba
            ? shadow_lba
            : original_lba;

    /*
     * Proteger cualquier region que
     * aun figure en un catalogo valido.
     */
    if (!binfs_region_available(
            index,
            target_lba
        )) {

        irq_restore(flags);
        return -1;
    }

    /* Escribir la nueva version */
    for (uint32_t s = 0;
         s < BINFS_SECTORS_PER_FILE;
         s++) {

        uint32_t offset =
            s * BINFS_SECTOR_SIZE;

        for (uint32_t j = 0;
             j < BINFS_SECTOR_SIZE;
             j++) {

            uint32_t pos = offset + j;

            sector[j] =
                pos < size
                    ? replace_buffer[pos]
                    : 0U;
        }

        if (ata_write_sector(
                target_lba + s,
                sector
            ) != 0) {

            irq_restore(flags);
            return -1;
        }
    }

    /* Verificar toda la region escrita */
    for (uint32_t s = 0;
         s < BINFS_SECTORS_PER_FILE;
         s++) {

        if (ata_read_sector(
                target_lba + s,
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

            uint32_t pos = offset + j;

            uint8_t expected =
                pos < size
                    ? replace_buffer[pos]
                    : 0U;

            if (sector[j] != expected) {
                irq_restore(flags);
                return -1;
            }
        }
    }

    /*
     * Los datos nuevos ya estan
     * escritos y verificados.
     */
    uint32_t hash =
        checksum32(replace_buffer, size);

    /*
     * Conservar el nombre y modificar
     * solamente LBA, tamano y checksum.
     */
    for (uint32_t i = 0; i < 4U; i++) {
        catalog[entry_offset + 32U + i] =
            (uint8_t)(target_lba >> (i * 8U));

        catalog[entry_offset + 36U + i] =
            (uint8_t)(size >> (i * 8U));

        catalog[entry_offset + 40U + i] =
            (uint8_t)(hash >> (i * 8U));
    }

    /*
     * Publicar el catalogo inactivo.
     * La funcion incrementa generacion
     * y calcula checksum de metadatos.
     */
    if (binfs_catalog_publish(
            catalog,
            generation
        ) != 0) {

        irq_restore(flags);
        return -1;
    }

    /* Comprobar la nueva version */
    if (binfs_load_catalog() != slots) {
        irq_restore(flags);
        return -1;
    }

    BinfsEntry updated;

    if (binfs_decode_entry(
            index,
            &updated
        ) != 0 ||
        !names_equal(
            destination,
            (const uint8_t *)updated.name
        ) ||
        updated.lba != target_lba ||
        updated.size != size) {

        irq_restore(flags);
        return -1;
    }

    irq_restore(flags);
    return 0;
}


/*
 * Comprobar que ninguna copia valida del
 * catalogo referencia la region de datos
 * que queremos sobrescribir.
 *
 * 1 = region disponible
 * 0 = region protegida o error
 */
static int binfs_region_available(
    uint32_t index,
    uint32_t target_lba
) {
    if (index >= BINFS_MAX_FILES)
        return 0;

    BinfsGenerationStatus status;

    if (binfs_generation_check(&status) != 0)
        return 0;

    /* Exigir ambas copias validas */
    if (!status.primary_valid ||
        !status.mirror_valid)
        return 0;

    if (ata_read_sector(
            BINFS_PRIMARY_CATALOG_LBA,
            copy_catalog_a
        ) != 0)
        return 0;

    if (ata_read_sector(
            BINFS_MIRROR_CATALOG_LBA,
            copy_catalog_b
        ) != 0)
        return 0;

    uint32_t slots_a =
        read_u32(&copy_catalog_a[8]);

    uint32_t slots_b =
        read_u32(&copy_catalog_b[8]);

    if (index >= slots_a || index >= slots_b)
        return 0;

    uint32_t offset =
        BINFS_FIRST_ENTRY +
        index * BINFS_ENTRY_SIZE;

    /*
     * Una entrada activa referencia
     * el LBA registrado en bytes 32-35.
     */
    if (copy_catalog_a[offset] != 0U) {
        uint32_t lba_a =
            read_u32(&copy_catalog_a[offset + 32U]);

        if (lba_a == target_lba)
            return 0;
    }

    if (copy_catalog_b[offset] != 0U) {
        uint32_t lba_b =
            read_u32(&copy_catalog_b[offset + 32U]);

        if (lba_b == target_lba)
            return 0;
    }

    return 1;
}
