#ifndef MIOS_DOWNLOAD_STORE_H
#define MIOS_DOWNLOAD_STORE_H
#include <stdint.h>

/* MiOS DOWNLOADFS 1.0: isolated extension volume, not legacy BINFS v2.
 * Requires separate QEMU IDE slave image (index=1).
 */
#define MIOS_DLSTORE_MAX_FILE_SIZE 65536U
#define MIOS_DLSTORE_MAX_FILES 12U
#define MIOS_DLSTORE_NAME_BYTES 24U

typedef struct {
    char name[MIOS_DLSTORE_NAME_BYTES];
    uint32_t size;
} MiosDlstoreEntry;

/* 0 success, -1 invalid/missing disk/error, -2 filename already used,
 * -3 no free slot. Does not overwrite existing files.
 */
int mios_dlstore_put(const char *name, const uint8_t *bytes, uint32_t size);
int mios_dlstore_get(const char *name, uint8_t *bytes, uint32_t capacity,
                     uint32_t *out_size);
int mios_dlstore_count(void);
int mios_dlstore_get_entry(uint32_t slot, MiosDlstoreEntry *result);
#endif
