#include <stdint.h>
#include "download_store.h"
#include "ata_slave.h"
#include "irq.h"

/* Dedicated ATA SLAVE volume, 1538 sectors minimum. This leaves the
 * existing MIOSFS/BINFS master disk and its A/B catalogs unchanged.
 *
 * On-disk catalog: two 512B sectors with generation and FNV checksum.
 * Sector 0: catalog A; sector 1: catalog B.
 * Sectors 2..1537: 12 independent slots of 128 sectors (64KiB).
 * Each entry is 40 bytes: name[24], size LE32, FNV32, status LE32,
 * reserved LE32. Exactly 12 entries after 32-byte header.
 */
#define DL_CAT_BYTES      512U
#define DL_CAT_A          0U
#define DL_CAT_B          1U
#define DL_DATA_START     2U
#define DL_SLOT_SECTORS   128U
#define DL_SECTOR_BYTES   512U
#define DL_ENTRY_BYTES    40U
#define DL_ENTRY_START    32U
#define DL_VERSION        1U
#define DL_MAGIC          "MIOSDL10"

static uint8_t dl_cat[DL_CAT_BYTES];
static uint8_t dl_tmp[DL_CAT_BYTES];
static uint8_t dl_sector[DL_SECTOR_BYTES];
static uint8_t dl_verify[DL_SECTOR_BYTES];

static uint32_t dl_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void dl_write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static uint32_t dl_hash(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261U;
    for (uint32_t i = 0U; i < n; i++) {
        h ^= p[i];
        h *= 16777619U;
    }
    return h;
}

static int dl_name_valid(const char *name)
{
    if (!name) return 0;
    uint32_t i = 0U;
    for (; i < MIOS_DLSTORE_NAME_BYTES; i++) {
        uint8_t c = (uint8_t)name[i];
        if (!c) return i > 0U && i < MIOS_DLSTORE_NAME_BYTES;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 0;
}

static int dl_name_match(const uint8_t *stored, const char *requested)
{
    for (uint32_t i = 0U; i < MIOS_DLSTORE_NAME_BYTES; i++) {
        if (stored[i] != (uint8_t)requested[i]) return 0;
        if (stored[i] == 0U) return 1;
    }
    return 0;
}

static int dl_catalog_valid(const uint8_t *cat)
{
    for (uint32_t i = 0U; i < 8U; i++) {
        if (cat[i] != (uint8_t)DL_MAGIC[i]) return 0;
    }
    if (dl_read32(cat + 8) != MIOS_DLSTORE_MAX_FILES ||
        dl_read32(cat + 12) != MIOS_DLSTORE_MAX_FILE_SIZE ||
        dl_read32(cat + 24) != DL_VERSION ||
        dl_read32(cat + 28) != 0U ||
        dl_read32(cat + 16) == 0U ||
        dl_read32(cat + 16) == 0xFFFFFFFFU)
        return 0;
    uint32_t h = 2166136261U;
    for (uint32_t i = 0U; i < DL_CAT_BYTES; i++) {
        uint8_t b = (i >= 20U && i < 24U) ? 0U : cat[i];
        h ^= b;
        h *= 16777619U;
    }
    if (dl_read32(cat + 20) != h) return 0;

    for (uint32_t slot = 0U; slot < MIOS_DLSTORE_MAX_FILES; slot++) {
        uint32_t off = DL_ENTRY_START + slot * DL_ENTRY_BYTES;
        uint32_t active = dl_read32(cat + off + 32U);
        if (active != 0U && active != 1U) return 0;
        if (active) {
            char name[MIOS_DLSTORE_NAME_BYTES];
            for (uint32_t i = 0U; i < MIOS_DLSTORE_NAME_BYTES; i++)
                name[i] = (char)cat[off + i];
            if (!dl_name_valid(name) ||
                dl_read32(cat + off + 24U) == 0U ||
                dl_read32(cat + off + 24U) > MIOS_DLSTORE_MAX_FILE_SIZE ||
                dl_read32(cat + off + 36U) != 0U)
                return 0;
            for (uint32_t other = 0U; other < slot; other++) {
                uint32_t off2 = DL_ENTRY_START + other * DL_ENTRY_BYTES;
                if (dl_read32(cat + off2 + 32U) &&
                    dl_name_match(cat + off2, name)) return 0;
            }
        } else {
            for (uint32_t i = 0U; i < DL_ENTRY_BYTES; i++)
                if (cat[off + i] != 0U) return 0;
        }
    }
    return 1;
}

/* Choose the latest individually valid A/B catalog. Invalid slave -> fail.
 * During a power loss after writing B but before A, B is recoverable.
 */
static int dl_load_catalog(void)
{
    if (ata_slave_read_sector(DL_CAT_A, dl_cat) != 0) return -1;
    if (ata_slave_read_sector(DL_CAT_B, dl_tmp) != 0) return -1;
    int a = dl_catalog_valid(dl_cat);
    int b = dl_catalog_valid(dl_tmp);
    if (!a && !b) return -1;
    if (b && (!a ||
        (int32_t)(dl_read32(dl_tmp + 16U) - dl_read32(dl_cat + 16U)) > 0)) {
        for (uint32_t i = 0U; i < DL_CAT_BYTES; i++) dl_cat[i] = dl_tmp[i];
    }
    return 0;
}

static int dl_publish(void)
{
    uint32_t generation = dl_read32(dl_cat + 16U);
    if (generation >= 0xFFFFFFFEU) return -1;
    dl_write32(dl_cat + 16U, generation + 1U);
    dl_write32(dl_cat + 20U, 0U);
    dl_write32(dl_cat + 20U, dl_hash(dl_cat, DL_CAT_BYTES));

    /* Write mirror first. On interrupted update, loader uses newest valid. */
    if (ata_slave_write_sector(DL_CAT_B, dl_cat) != 0 ||
        ata_slave_read_sector(DL_CAT_B, dl_tmp) != 0)
        return -1;
    for (uint32_t i = 0U; i < DL_CAT_BYTES; i++)
        if (dl_tmp[i] != dl_cat[i]) return -1;
    if (ata_slave_write_sector(DL_CAT_A, dl_cat) != 0 ||
        ata_slave_read_sector(DL_CAT_A, dl_tmp) != 0)
        return -1;
    for (uint32_t i = 0U; i < DL_CAT_BYTES; i++)
        if (dl_tmp[i] != dl_cat[i]) return -1;
    return 0;
}

int mios_dlstore_put(const char *name, const uint8_t *data, uint32_t size)
{
    if (!dl_name_valid(name) || !data || size == 0U ||
        size > MIOS_DLSTORE_MAX_FILE_SIZE) return -1;
    uint32_t flags = irq_save();
    if (dl_load_catalog() != 0) { irq_restore(flags); return -1; }
    int free_slot = -1;
    for (uint32_t slot = 0U; slot < MIOS_DLSTORE_MAX_FILES; slot++) {
        uint32_t off = DL_ENTRY_START + slot * DL_ENTRY_BYTES;
        if (dl_read32(dl_cat + off + 32U)) {
            if (dl_name_match(dl_cat + off, name)) {
                irq_restore(flags);
                return -2; /* Never overwrite. */
            }
        } else if (free_slot < 0) free_slot = (int)slot;
    }
    if (free_slot < 0) { irq_restore(flags); return -3; }

    uint32_t start = DL_DATA_START + (uint32_t)free_slot * DL_SLOT_SECTORS;
    uint32_t sectors = (size + DL_SECTOR_BYTES - 1U) / DL_SECTOR_BYTES;
    for (uint32_t s = 0U; s < sectors; s++) {
        uint32_t offset = s * DL_SECTOR_BYTES;
        for (uint32_t i = 0U; i < DL_SECTOR_BYTES; i++) {
            uint32_t at = offset + i;
            dl_sector[i] = (at < size) ? data[at] : 0U;
        }
        if (ata_slave_write_sector(start + s, dl_sector) != 0 ||
            ata_slave_read_sector(start + s, dl_verify) != 0) {
            irq_restore(flags); return -1;
        }
        for (uint32_t i = 0U; i < DL_SECTOR_BYTES; i++) {
            if (dl_verify[i] != dl_sector[i]) {
                irq_restore(flags); return -1;
            }
        }
    }
    uint32_t off = DL_ENTRY_START + (uint32_t)free_slot * DL_ENTRY_BYTES;
    for (uint32_t i = 0U; i < DL_ENTRY_BYTES; i++) dl_cat[off + i] = 0U;
    for (uint32_t i = 0U; name[i]; i++) dl_cat[off + i] = (uint8_t)name[i];
    dl_write32(dl_cat + off + 24U, size);
    dl_write32(dl_cat + off + 28U, dl_hash(data, size));
    dl_write32(dl_cat + off + 32U, 1U);
    int rc = dl_publish();
    irq_restore(flags);
    return rc;
}

int mios_dlstore_get(const char *name, uint8_t *out, uint32_t cap,
                     uint32_t *out_size)
{
    if (out_size) *out_size = 0U;
    if (!dl_name_valid(name) || !out || !out_size) return -1;
    uint32_t flags = irq_save();
    if (dl_load_catalog() != 0) { irq_restore(flags); return -1; }
    for (uint32_t slot = 0U; slot < MIOS_DLSTORE_MAX_FILES; slot++) {
        uint32_t off = DL_ENTRY_START + slot * DL_ENTRY_BYTES;
        if (dl_read32(dl_cat + off + 32U) == 0U ||
            !dl_name_match(dl_cat + off, name)) continue;
        uint32_t size = dl_read32(dl_cat + off + 24U);
        if (size > cap) { irq_restore(flags); return -1; }
        uint32_t start = DL_DATA_START + slot * DL_SLOT_SECTORS;
        uint32_t sectors = (size + DL_SECTOR_BYTES - 1U) / DL_SECTOR_BYTES;
        for (uint32_t s = 0U; s < sectors; s++) {
            if (ata_slave_read_sector(start + s, dl_sector) != 0) {
                irq_restore(flags); return -1;
            }
            uint32_t base = s * DL_SECTOR_BYTES;
            uint32_t bytes = size - base;
            if (bytes > DL_SECTOR_BYTES) bytes = DL_SECTOR_BYTES;
            for (uint32_t i = 0U; i < bytes; i++) out[base + i] = dl_sector[i];
        }
        if (dl_hash(out, size) != dl_read32(dl_cat + off + 28U)) {
            irq_restore(flags); return -1;
        }
        *out_size = size;
        irq_restore(flags);
        return 0;
    }
    irq_restore(flags);
    return -1;
}

int mios_dlstore_count(void)
{
    uint32_t flags = irq_save();
    if (dl_load_catalog() != 0) { irq_restore(flags); return -1; }
    int total = 0;
    for (uint32_t slot = 0U; slot < MIOS_DLSTORE_MAX_FILES; slot++)
        if (dl_read32(dl_cat + DL_ENTRY_START + slot * DL_ENTRY_BYTES + 32U))
            total++;
    irq_restore(flags);
    return total;
}

int mios_dlstore_get_entry(uint32_t slot, MiosDlstoreEntry *result)
{
    if (!result || slot >= MIOS_DLSTORE_MAX_FILES) return -1;
    uint32_t flags = irq_save();
    if (dl_load_catalog() != 0) { irq_restore(flags); return -1; }
    uint32_t off = DL_ENTRY_START + slot * DL_ENTRY_BYTES;
    if (!dl_read32(dl_cat + off + 32U)) { irq_restore(flags); return -1; }
    for (uint32_t i = 0U; i < MIOS_DLSTORE_NAME_BYTES; i++)
        result->name[i] = (char)dl_cat[off + i];
    result->name[MIOS_DLSTORE_NAME_BYTES - 1U] = 0;
    result->size = dl_read32(dl_cat + off + 24U);
    irq_restore(flags);
    return 0;
}
