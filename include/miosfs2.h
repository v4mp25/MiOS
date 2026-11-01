
#ifndef MIOS_FS2_H
#define MIOS_FS2_H

#include <stdint.h>

#define MIOSFS2_VERSION 2U
#define MIOSFS2_SECTOR_SIZE 512U
#define MIOSFS2_MAX_NODES 64U

#define MIOSFS2_SUPER_LBA 2048U
#define MIOSFS2_BITMAP_LBA 2049U
#define MIOSFS2_NODES_LBA 2050U
#define MIOSFS2_DATA_LBA 2058U

typedef struct __attribute__((packed)) {
    char magic[8];

    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;

    uint32_t bitmap_lba;
    uint32_t bitmap_sectors;

    uint32_t nodes_lba;
    uint32_t nodes_sectors;

    uint32_t data_lba;
    uint32_t data_sectors;

    uint32_t root_inode;
    uint32_t max_nodes;

    uint32_t flags;
    uint32_t checksum;
    uint32_t reserved;
} MiosFs2Super;

typedef struct __attribute__((packed)) {
    char name[32];

    uint32_t parent;
    uint32_t first_block;
    uint32_t size;
    uint32_t flags;
    uint32_t block_count;

    uint32_t reserved[3];
} MiosFs2Node;

void miosfs2_make_test_super(
    MiosFs2Super *sb
);

uint32_t miosfs2_checksum(
    const MiosFs2Super *sb
);

int miosfs2_validate(
    const MiosFs2Super *sb
);

int miosfs2_selftest(void);

#endif
