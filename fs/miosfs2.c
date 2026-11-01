
#include <stdint.h>
#include <stddef.h>

#include "miosfs2.h"

/*
 * Verificar los tamaños de estructuras.
 */
typedef char fs2_super_size_check[
    sizeof(MiosFs2Super) == 64 ? 1 : -1
];

typedef char fs2_node_size_check[
    sizeof(MiosFs2Node) == 64 ? 1 : -1
];

static const char fs2_magic[8] = {
    'M', 'I', 'O', 'S', 'F', 'S', '0', '2'
};

/*
 * Calcular checksum FNV-1a.
 */
uint32_t miosfs2_checksum(
    const MiosFs2Super *sb
) {
    if (sb == NULL)
        return 0;

    const uint8_t *bytes =
        (const uint8_t *)sb;

    const uint32_t hash_offset =
        (uint32_t)offsetof(
            MiosFs2Super,
            checksum
        );

    uint32_t hash = 2166136261U;

    for (uint32_t i = 0;
         i < sizeof(MiosFs2Super);
         i++) {

        uint8_t value = bytes[i];

        if (i >= hash_offset &&
            i < hash_offset + 4U) {
            value = 0;
        }

        hash ^= value;
        hash *= 16777619U;
    }

    return hash;
}

/*
 * Crear superbloque experimental en RAM.
 */
void miosfs2_make_test_super(
    MiosFs2Super *sb
) {
    if (sb == NULL)
        return;

    uint8_t *bytes = (uint8_t *)sb;

    for (uint32_t i = 0;
         i < sizeof(MiosFs2Super);
         i++) {
        bytes[i] = 0;
    }

    for (uint32_t i = 0; i < 8U; i++) {
        sb->magic[i] = fs2_magic[i];
    }

    sb->version = MIOSFS2_VERSION;
    sb->sector_size = MIOSFS2_SECTOR_SIZE;
    sb->total_sectors = 32768U;

    sb->bitmap_lba = MIOSFS2_BITMAP_LBA;
    sb->bitmap_sectors = 1U;

    sb->nodes_lba = MIOSFS2_NODES_LBA;
    sb->nodes_sectors = 8U;

    sb->data_lba = MIOSFS2_DATA_LBA;
    sb->data_sectors = 1024U;

    sb->root_inode = 0U;
    sb->max_nodes = MIOSFS2_MAX_NODES;

    sb->flags = 0U;
    sb->reserved = 0U;

    sb->checksum = miosfs2_checksum(sb);
}

/*
 * Validar superbloque.
 */
int miosfs2_validate(
    const MiosFs2Super *sb
) {
    if (sb == NULL)
        return -1;

    for (uint32_t i = 0; i < 8U; i++) {
        if (sb->magic[i] != fs2_magic[i])
            return -1;
    }

    if (sb->version != MIOSFS2_VERSION ||
        sb->sector_size != MIOSFS2_SECTOR_SIZE ||
        sb->total_sectors == 0U) {
        return -1;
    }

    if (sb->max_nodes != MIOSFS2_MAX_NODES ||
        sb->root_inode != 0U ||
        sb->flags != 0U ||
        sb->reserved != 0U) {
        return -1;
    }

    if (sb->bitmap_sectors == 0U ||
        sb->nodes_sectors < 8U ||
        sb->data_sectors == 0U) {
        return -1;
    }

    /*
     * 1 sector de bitmap representa
     * hasta 4096 bloques.
     */
    if (sb->data_sectors >
        sb->bitmap_sectors * 4096U) {
        return -1;
    }

    if (sb->bitmap_lba >= sb->total_sectors ||
        sb->bitmap_sectors >
            sb->total_sectors - sb->bitmap_lba) {
        return -1;
    }

    uint32_t bitmap_end =
        sb->bitmap_lba + sb->bitmap_sectors;

    if (sb->nodes_lba < bitmap_end ||
        sb->nodes_lba >= sb->total_sectors ||
        sb->nodes_sectors >
            sb->total_sectors - sb->nodes_lba) {
        return -1;
    }

    uint32_t nodes_end =
        sb->nodes_lba + sb->nodes_sectors;

    if (sb->data_lba < nodes_end ||
        sb->data_lba >= sb->total_sectors ||
        sb->data_sectors >
            sb->total_sectors - sb->data_lba) {
        return -1;
    }

    if (sb->checksum != miosfs2_checksum(sb))
        return -1;

    return 0;
}

/*
 * Pruebas del formato MIOSFS02 en RAM.
 *
 * No modifica ningun sector del disco.
 */
int miosfs2_selftest(void) {
    MiosFs2Super sb;

    miosfs2_make_test_super(&sb);

    if (miosfs2_validate(&sb) != 0)
        return -1;

    /* Detectar checksum incorrecto */
    sb.checksum ^= 1U;

    if (miosfs2_validate(&sb) == 0)
        return -1;

    miosfs2_make_test_super(&sb);

    /* Detectar version incorrecta */
    sb.version = 99U;
    sb.checksum = miosfs2_checksum(&sb);

    if (miosfs2_validate(&sb) == 0)
        return -1;

    miosfs2_make_test_super(&sb);

    /* Detectar regiones superpuestas */
    sb.nodes_lba = sb.bitmap_lba;
    sb.checksum = miosfs2_checksum(&sb);

    if (miosfs2_validate(&sb) == 0)
        return -1;

    miosfs2_make_test_super(&sb);

    /* Detectar exceso de sectores */
    sb.data_sectors = sb.total_sectors;
    sb.checksum = miosfs2_checksum(&sb);

    if (miosfs2_validate(&sb) == 0)
        return -1;

    return 0;
}
