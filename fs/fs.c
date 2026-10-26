#include "fs.h"
#include "ata.h"

#define MAX_NODES 64
#define NAME_SIZE 32
#define CONTENT_SIZE 256
#define PATH_SIZE 128

/* LBA 100 se reserva para disktest; LBA 101 es el superblock.
   Los nodos serializados comienzan en LBA 102. */
#define FS_MAGIC "MIOSFS01"
#define FS_VERSION 1U
#define FS_SUPER_LBA 101U
#define FS_DATA_LBA 102U
#define FS_RECORD_SIZE 292U
#define FS_DATA_BYTES (MAX_NODES * FS_RECORD_SIZE)
#define FS_DATA_SECTORS ((FS_DATA_BYTES + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE)

typedef struct {
    int used;
    int is_dir;
    int parent;
    char name[NAME_SIZE];
    char content[CONTENT_SIZE];
} FsNode;

static FsNode nodes[MAX_NODES];
static uint8_t disk_buffer[FS_DATA_SECTORS * ATA_SECTOR_SIZE];
static uint8_t sector_buffer[ATA_SECTOR_SIZE];
static int persistence_ready = 0;

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int copy_text(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (!cap) return MIOS_FS_BUFFER;
    while (src[i]) {
        if (i + 1 >= cap) return MIOS_FS_BUFFER;
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return MIOS_FS_OK;
}

static int append_text(char *out, size_t cap, size_t *used,
                       const char *s) {
    size_t n = str_len(s);
    if (*used + n >= cap) return MIOS_FS_BUFFER;
    for (size_t i = 0; i < n; i++) out[(*used)++] = s[i];
    out[*used] = '\0';
    return MIOS_FS_OK;
}

static int str_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int find_child(int parent, const char *name, size_t len) {
    for (int i = 1; i < MAX_NODES; i++) {
        if (!nodes[i].used || nodes[i].parent != parent) continue;
        size_t n = str_len(nodes[i].name);
        if (n == len && str_eq_n(nodes[i].name, name, len)) return i;
    }
    return MIOS_FS_NOT_FOUND;
}

static void init_nodes(void) {
    for (int i = 0; i < MAX_NODES; i++) {
        nodes[i].used = 0;
        nodes[i].is_dir = 0;
        nodes[i].parent = 0;
        nodes[i].name[0] = '\0';
        nodes[i].content[0] = '\0';
    }
    nodes[0].used = 1;
    nodes[0].is_dir = 1;
    nodes[0].parent = 0;
}

/* FNV-1a de 32 bits para detectar una tabla dañada. */
static uint32_t checksum32(const uint8_t *data, size_t len) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

static void serialize_nodes(void) {
    for (size_t i = 0; i < sizeof(disk_buffer); i++)
        disk_buffer[i] = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        size_t off = (size_t)i * FS_RECORD_SIZE;
        disk_buffer[off] = (uint8_t)(nodes[i].used != 0);
        disk_buffer[off + 1] = (uint8_t)(nodes[i].is_dir != 0);
        disk_buffer[off + 2] = (uint8_t)(nodes[i].parent & 0xFF);
        disk_buffer[off + 3] = (uint8_t)((nodes[i].parent >> 8) & 0xFF);
        for (size_t j = 0; j < NAME_SIZE; j++)
            disk_buffer[off + 4 + j] = (uint8_t)nodes[i].name[j];
        for (size_t j = 0; j < CONTENT_SIZE; j++)
            disk_buffer[off + 4 + NAME_SIZE + j] =
                (uint8_t)nodes[i].content[j];
    }
}

static int deserialize_nodes(void) {
    static FsNode candidate[MAX_NODES];

    for (int i = 0; i < MAX_NODES; i++) {
        size_t off = (size_t)i * FS_RECORD_SIZE;
        candidate[i].used = disk_buffer[off] ? 1 : 0;
        candidate[i].is_dir = disk_buffer[off + 1] ? 1 : 0;
        candidate[i].parent = (int)disk_buffer[off + 2] |
                              ((int)disk_buffer[off + 3] << 8);
        for (size_t j = 0; j < NAME_SIZE; j++)
            candidate[i].name[j] = (char)disk_buffer[off + 4 + j];
        for (size_t j = 0; j < CONTENT_SIZE; j++)
            candidate[i].content[j] =
                (char)disk_buffer[off + 4 + NAME_SIZE + j];

        candidate[i].name[NAME_SIZE - 1] = '\0';
        candidate[i].content[CONTENT_SIZE - 1] = '\0';

        if (candidate[i].used &&
            (candidate[i].parent < 0 || candidate[i].parent >= MAX_NODES))
            return -1;
    }

    if (!candidate[0].used || !candidate[0].is_dir ||
        candidate[0].parent != 0)
        return -1;

    for (int i = 1; i < MAX_NODES; i++) {
        if (candidate[i].used && !candidate[candidate[i].parent].used)
            return -1;
    }

    for (int i = 0; i < MAX_NODES; i++) nodes[i] = candidate[i];
    return 0;
}

/* Escribe la tabla y el superblock al final. */
static int save_fs(void) {
    uint8_t super[ATA_SECTOR_SIZE];
    serialize_nodes();
    uint32_t sum = checksum32(disk_buffer, FS_DATA_BYTES);

    for (uint32_t s = 0; s < FS_DATA_SECTORS; s++) {
        size_t off = (size_t)s * ATA_SECTOR_SIZE;
        for (size_t j = 0; j < ATA_SECTOR_SIZE; j++)
            sector_buffer[j] = disk_buffer[off + j];
        if (ata_write_sector(FS_DATA_LBA + s, sector_buffer) != 0)
            return -1;
    }

    for (size_t i = 0; i < ATA_SECTOR_SIZE; i++) super[i] = 0;
    for (size_t i = 0; i < 8; i++) super[i] = (uint8_t)FS_MAGIC[i];

    super[8] = (uint8_t)FS_VERSION;
    super[9] = (uint8_t)(FS_VERSION >> 8);
    super[10] = (uint8_t)(FS_VERSION >> 16);
    super[11] = (uint8_t)(FS_VERSION >> 24);
    super[12] = (uint8_t)MAX_NODES;
    super[13] = (uint8_t)(MAX_NODES >> 8);
    super[14] = (uint8_t)FS_RECORD_SIZE;
    super[15] = (uint8_t)(FS_RECORD_SIZE >> 8);
    super[16] = (uint8_t)sum;
    super[17] = (uint8_t)(sum >> 8);
    super[18] = (uint8_t)(sum >> 16);
    super[19] = (uint8_t)(sum >> 24);
    super[20] = (uint8_t)FS_DATA_SECTORS;
    super[21] = (uint8_t)(FS_DATA_SECTORS >> 8);

    if (ata_write_sector(FS_SUPER_LBA, super) != 0)
        return -1;

    persistence_ready = 1;
    return 0;
}

/* 0=cargado; 1=disco sin formato MiOS; -1=fallo I/O; -2=datos invalidos. */
static int load_fs(void) {
    uint8_t super[ATA_SECTOR_SIZE];

    if (ata_read_sector(FS_SUPER_LBA, super) != 0)
        return -1;

    for (size_t i = 0; i < 8; i++)
        if (super[i] != (uint8_t)FS_MAGIC[i]) return 1;

    uint32_t version = (uint32_t)super[8] |
        ((uint32_t)super[9] << 8) |
        ((uint32_t)super[10] << 16) |
        ((uint32_t)super[11] << 24);
    uint16_t count = (uint16_t)super[12] |
        ((uint16_t)super[13] << 8);
    uint16_t record_size = (uint16_t)super[14] |
        ((uint16_t)super[15] << 8);
    uint32_t expected = (uint32_t)super[16] |
        ((uint32_t)super[17] << 8) |
        ((uint32_t)super[18] << 16) |
        ((uint32_t)super[19] << 24);
    uint16_t sectors = (uint16_t)super[20] |
        ((uint16_t)super[21] << 8);

    if (version != FS_VERSION || count != MAX_NODES ||
        record_size != FS_RECORD_SIZE || sectors != FS_DATA_SECTORS)
        return -2;

    for (uint32_t s = 0; s < FS_DATA_SECTORS; s++) {
        if (ata_read_sector(FS_DATA_LBA + s, sector_buffer) != 0)
            return -1;
        size_t off = (size_t)s * ATA_SECTOR_SIZE;
        for (size_t j = 0; j < ATA_SECTOR_SIZE; j++)
            disk_buffer[off + j] = sector_buffer[j];
    }

    if (checksum32(disk_buffer, FS_DATA_BYTES) != expected)
        return -2;
    if (deserialize_nodes() != 0)
        return -2;

    persistence_ready = 1;
    return 0;
}

void mios_fs_init(void) {
    init_nodes();
    persistence_ready = 0;

    int status = load_fs();
    if (status == 0) return;

    /* Solo formatea un disco virgen. Si hay I/O o metadata incompatible,
       conserva la raiz en RAM y no sobrescribe el disco automaticamente. */
    if (status == 1 && save_fs() == 0)
        return;
}

static int resolve(uint16_t cwd, const char *path) {
    if (cwd >= MAX_NODES || !nodes[cwd].used)
        return MIOS_FS_BAD_PATH;
    if (!path || !path[0]) return cwd;

    int current = (path[0] == '/') ? 0 : (int)cwd;
    size_t i = 0;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;

        size_t start = i;
        while (path[i] && path[i] != '/') i++;
        size_t len = i - start;

        if (len == 1 && path[start] == '.') continue;

        if (len == 2 && path[start] == '.' && path[start + 1] == '.') {
            current = nodes[current].parent;
            continue;
        }

        if (!nodes[current].is_dir) return MIOS_FS_NOT_DIR;

        int child = find_child(current, path + start, len);
        if (child < 0) return child;
        current = child;
    }

    return current;
}

static int split_path(uint16_t cwd, const char *path,
                      int *parent, char *name) {
    char temp[PATH_SIZE];
    size_t len;

    if (!path || !path[0] || str_len(path) >= PATH_SIZE)
        return MIOS_FS_BAD_PATH;
    if (copy_text(temp, path, sizeof(temp)) != MIOS_FS_OK)
        return MIOS_FS_BAD_PATH;

    len = str_len(temp);
    while (len > 1 && temp[len - 1] == '/')
        temp[--len] = '\0';
    if (len == 1 && temp[0] == '/')
        return MIOS_FS_BAD_PATH;

    size_t slash = len;
    while (slash > 0 && temp[slash - 1] != '/') slash--;
    char *base = temp + slash;

    if (!base[0] ||
        (base[0] == '.' && base[1] == '\0') ||
        (base[0] == '.' && base[1] == '.' && base[2] == '\0'))
        return MIOS_FS_BAD_PATH;
    if (str_len(base) >= NAME_SIZE) return MIOS_FS_BAD_PATH;

    if (slash == 0) {
        *parent = cwd;
    } else if (slash == 1) {
        *parent = 0;
    } else {
        temp[slash - 1] = '\0';
        *parent = resolve(cwd, temp);
        if (*parent < 0) return *parent;
    }

    if (!nodes[*parent].is_dir) return MIOS_FS_NOT_DIR;
    return copy_text(name, base, NAME_SIZE);
}

static int create_node(uint16_t cwd, const char *path, int is_dir) {
    int parent;
    char name[NAME_SIZE];
    int result = split_path(cwd, path, &parent, name);
    if (result < 0) return result;

    if (find_child(parent, name, str_len(name)) >= 0)
        return MIOS_FS_EXISTS;

    int slot = -1;
    for (int i = 1; i < MAX_NODES; i++) {
        if (!nodes[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return MIOS_FS_FULL;

    nodes[slot].used = 1;
    nodes[slot].is_dir = is_dir;
    nodes[slot].parent = parent;
    copy_text(nodes[slot].name, name, NAME_SIZE);
    nodes[slot].content[0] = '\0';

    if (save_fs() != 0) {
        nodes[slot].used = 0;
        return MIOS_FS_IO;
    }
    return MIOS_FS_OK;
}

int mios_fs_cd(uint16_t cwd, const char *path) {
    int node = resolve(cwd, path);
    if (node < 0) return node;
    if (!nodes[node].is_dir) return MIOS_FS_NOT_DIR;
    return node;
}

int mios_fs_pwd(uint16_t cwd, char *out, size_t cap) {
    if (cwd >= MAX_NODES || !nodes[cwd].used)
        return MIOS_FS_BAD_PATH;
    if (cap < 2) return MIOS_FS_BUFFER;

    int ancestors[MAX_NODES];
    size_t count = 0;
    int current = cwd;

    while (current != 0 && count < MAX_NODES) {
        ancestors[count++] = current;
        current = nodes[current].parent;
    }
    if (current != 0) return MIOS_FS_BAD_PATH;

    size_t used = 0;
    out[0] = '\0';
    int result = append_text(out, cap, &used, "/");
    if (result < 0) return result;

    for (size_t i = count; i > 0; i--) {
        result = append_text(out, cap, &used,
                             nodes[ancestors[i - 1]].name);
        if (result < 0) return result;
        if (i > 1) {
            result = append_text(out, cap, &used, "/");
            if (result < 0) return result;
        }
    }
    return MIOS_FS_OK;
}

int mios_fs_ls(uint16_t cwd, const char *path,
               char *out, size_t cap) {
    int dir = resolve(cwd, path);
    if (dir < 0) return dir;
    if (!nodes[dir].is_dir) return MIOS_FS_NOT_DIR;
    if (cap == 0) return MIOS_FS_BUFFER;

    size_t used = 0;
    out[0] = '\0';

    for (int i = 1; i < MAX_NODES; i++) {
        if (!nodes[i].used || nodes[i].parent != dir) continue;

        int result = append_text(out, cap, &used, nodes[i].name);
        if (result < 0) return result;

        if (nodes[i].is_dir) {
            result = append_text(out, cap, &used, "/");
            if (result < 0) return result;
        }

        result = append_text(out, cap, &used, "\n");
        if (result < 0) return result;
    }

    return MIOS_FS_OK;
}

int mios_fs_mkdir(uint16_t cwd, const char *path) {
    return create_node(cwd, path, 1);
}

int mios_fs_touch(uint16_t cwd, const char *path) {
    return create_node(cwd, path, 0);
}

int mios_fs_cat(uint16_t cwd, const char *path,
                char *out, size_t cap) {
    int node = resolve(cwd, path);
    if (node < 0) return node;
    if (nodes[node].is_dir) return MIOS_FS_IS_DIR;
    return copy_text(out, nodes[node].content, cap);
}

int mios_fs_write(uint16_t cwd, const char *path,
                  const char *text) {
    int node = resolve(cwd, path);

    if (node == MIOS_FS_NOT_FOUND) {
        int result = create_node(cwd, path, 0);
        if (result < 0) return result;
        node = resolve(cwd, path);
    }

    if (node < 0) return node;
    if (nodes[node].is_dir) return MIOS_FS_IS_DIR;

    char previous[CONTENT_SIZE];
    copy_text(previous, nodes[node].content, sizeof(previous));

    int result = copy_text(nodes[node].content, text, CONTENT_SIZE);
    if (result < 0) return result;

    if (save_fs() != 0) {
        copy_text(nodes[node].content, previous, sizeof(previous));
        return MIOS_FS_IO;
    }

    return MIOS_FS_OK;
}


/* ---------------- Operaciones adicionales: tree, rm y rmdir ---------------- */

static int tree_walk(int directory, unsigned depth,
                     char *out, size_t cap, size_t *used) {
    static const char indent[] = "  ";
    if (depth > MAX_NODES) return MIOS_FS_BAD_PATH;

    for (int i = 1; i < MAX_NODES; i++) {
        if (!nodes[i].used || nodes[i].parent != directory) continue;

        for (unsigned d = 0; d < depth; d++) {
            int r = append_text(out, cap, used, indent);
            if (r < 0) return r;
        }

        int r = append_text(out, cap, used, nodes[i].name);
        if (r < 0) return r;
        if (nodes[i].is_dir) {
            r = append_text(out, cap, used, "/");
            if (r < 0) return r;
        }
        r = append_text(out, cap, used, "\n");
        if (r < 0) return r;

        if (nodes[i].is_dir) {
            r = tree_walk(i, depth + 1, out, cap, used);
            if (r < 0) return r;
        }
    }
    return MIOS_FS_OK;
}

int mios_fs_tree(uint16_t cwd, const char *path,
                 char *out, size_t cap) {
    int directory = resolve(cwd, path);
    if (directory < 0) return directory;
    if (!nodes[directory].is_dir) return MIOS_FS_NOT_DIR;
    if (cap == 0) return MIOS_FS_BUFFER;
    size_t used = 0;
    out[0] = '\0';
    return tree_walk(directory, 0, out, cap, &used);
}

int mios_fs_rm(uint16_t cwd, const char *path) {
    int node = resolve(cwd, path);
    if (node < 0) return node;
    if (node == 0) return MIOS_FS_BAD_PATH;
    if (nodes[node].is_dir) return MIOS_FS_IS_DIR;

    FsNode previous = nodes[node];
    nodes[node].used = 0;
    nodes[node].is_dir = 0;
    nodes[node].parent = 0;
    nodes[node].name[0] = '\0';
    nodes[node].content[0] = '\0';

    if (save_fs() != 0) {
        nodes[node] = previous;
        return MIOS_FS_IO;
    }
    return MIOS_FS_OK;
}

int mios_fs_rmdir(uint16_t cwd, const char *path) {
    int node = resolve(cwd, path);
    if (node < 0) return node;
    if (node == 0 || node == (int)cwd) return MIOS_FS_BAD_PATH;
    if (!nodes[node].is_dir) return MIOS_FS_NOT_DIR;

    for (int i = 1; i < MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == node)
            return MIOS_FS_NOT_EMPTY;
    }

    FsNode previous = nodes[node];
    nodes[node].used = 0;
    nodes[node].is_dir = 0;
    nodes[node].parent = 0;
    nodes[node].name[0] = '\0';
    nodes[node].content[0] = '\0';

    if (save_fs() != 0) {
        nodes[node] = previous;
        return MIOS_FS_IO;
    }
    return MIOS_FS_OK;
}
