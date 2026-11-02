
#include <stdint.h>
#include <stddef.h>

#include "gui_files.h"
#include "fs.h"
#include "binfs.h"

#define GUI_FILES_MAX 64
#define GUI_FILES_VISIBLE 7

#define COLOR_NORMAL 0x70
#define COLOR_SELECTED 0x1F

#define SC_ENTER 0x1C
#define SC_BACKSPACE 0x0E
#define SC_UP 0x48
#define SC_DOWN 0x50
#define SC_A 0x1E
#define SC_B 0x30
#define SC_R 0x13

typedef struct {
    char name[33];
    uint32_t size;
    uint32_t lba;
    int is_dir;
} GuiFile;

static volatile uint16_t *const video =
    (volatile uint16_t *)0xB8000;

static GuiFile files[GUI_FILES_MAX];
static int file_count = 0;
static int selected = 0;
static int mode = 0;     /* 0 MIOSFS01, 1 BINFS */
static int cwd = 0;
static int details = 0;
static int load_error = 0;

static char listing[4096];
static char content[256];
static char current_path[128];

/* Caracter VGA */
static void putc_at(
    int x, int y, char c, uint8_t color
) {
    if (x < 29 || x > 75 ||
        y < 5 || y > 19)
        return;

    video[y * 80 + x] =
        ((uint16_t)color << 8) |
        (uint8_t)c;
}

/* Texto VGA con limite de ancho */
static void print_at(
    int x, int y,
    const char *s,
    uint8_t color,
    int max
) {
    if (s == NULL)
        return;

    for (int i = 0;
         s[i] && i < max && x + i <= 75;
         i++) {

        unsigned char c = (unsigned char)s[i];

        putc_at(
            x + i, y,
            (c >= 32U && c <= 126U)
                ? (char)c : '.',
            color
        );
    }
}

/* Convertir un numero a decimal */
static void print_number(
    int x, int y,
    uint32_t value,
    uint8_t color
) {
    char buf[11];
    int n = 0;

    do {
        buf[n++] = (char)(
            '0' + value % 10U
        );
        value /= 10U;
    } while (value && n < 10);

    for (int i = n - 1; i >= 0; i--) {
        putc_at(x++, y, buf[i], color);
    }
}

/* Limpiar solamente el interior */
static void clear_area(void) {
    for (int y = 5; y <= 19; y++) {
        for (int x = 29; x <= 75; x++) {
            putc_at(
                x, y, ' ',
                COLOR_NORMAL
            );
        }
    }
}

static void copy_name(
    char *dst,
    const char *src,
    uint32_t max
) {
    uint32_t i = 0;

    while (i + 1U < max && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

/*
 * Leer una lista en RAM.
 * No modificar archivos.
 */
static void refresh_files(void) {
    file_count = 0;
    selected = 0;
    details = 0;
    load_error = 0;
    content[0] = '\0';

    if (mode == 0) {
        if (mios_fs_pwd(
                (uint16_t)cwd,
                current_path,
                sizeof(current_path)
            ) != MIOS_FS_OK) {
            load_error = 1;
            return;
        }

        if (mios_fs_ls(
                (uint16_t)cwd,
                ".",
                listing,
                sizeof(listing)
            ) != MIOS_FS_OK) {
            load_error = 1;
            return;
        }

        /*
         * Cada linea termina en '\n'.
         * Un '/' final identifica directorios.
         */
        uint32_t pos = 0;

        while (listing[pos] &&
               file_count < GUI_FILES_MAX) {

            char name[33];
            uint32_t n = 0;
            int is_dir = 0;

            while (listing[pos] &&
                   listing[pos] != '\n') {

                char c = listing[pos++];

                if (c == '/') {
                    is_dir = 1;
                } else if (n < 32U) {
                    name[n++] = c;
                }
            }

            if (listing[pos] == '\n')
                pos++;

            name[n] = '\0';

            if (n == 0U)
                continue;

            GuiFile *entry = &files[file_count++];

            copy_name(
                entry->name,
                name,
                sizeof(entry->name)
            );

            entry->is_dir = is_dir;
            entry->size = 0;
            entry->lba = 0;
        }

    } else {
        copy_name(
            current_path,
            "BINFS:/",
            sizeof(current_path)
        );

        int count = binfs_count();

        if (count < 0) {
            load_error = 1;
            return;
        }

        if (count > GUI_FILES_MAX)
            count = GUI_FILES_MAX;

        for (int i = 0; i < count; i++) {
            BinfsEntry entry;

            if (binfs_get_entry(
                    (uint32_t)i,
                    &entry
                ) != 0) {
                file_count = 0;
                load_error = 1;
                return;
            }

            GuiFile *item = &files[file_count++];

            copy_name(
                item->name,
                entry.name,
                sizeof(item->name)
            );

            item->size = entry.size;
            item->lba = entry.lba;
            item->is_dir = 0;
        }
    }
}

void gui_files_open(void) {
    mode = 0;
    cwd = 0;
    refresh_files();
}

/*
 * Ver contenido de un archivo MIOSFS01.
 * BINFS muestra metadatos, no texto ELF.
 */
static void open_selected(void) {
    if (selected >= file_count)
        return;

    GuiFile *item = &files[selected];

    if (mode == 0 && item->is_dir) {
        int next = mios_fs_cd(
            (uint16_t)cwd,
            item->name
        );

        if (next >= 0) {
            cwd = next;
            refresh_files();
        }
        return;
    }

    details = 1;

    if (mode == 0) {
        if (mios_fs_cat(
                (uint16_t)cwd,
                item->name,
                content,
                sizeof(content)
            ) != MIOS_FS_OK) {

            copy_name(
                content,
                "No se pudo leer el archivo.",
                sizeof(content)
            );
        }
    }
}

int gui_files_key(uint8_t scancode) {
    if (scancode == SC_A) {
        mode = 0;
        cwd = 0;
        refresh_files();
        return 1;
    }

    if (scancode == SC_B) {
        mode = 1;
        refresh_files();
        return 1;
    }

    if (scancode == SC_R) {
        refresh_files();
        return 1;
    }

    if (scancode == SC_UP) {
        if (selected > 0)
            selected--;

        details = 0;
        return 1;
    }

    if (scancode == SC_DOWN) {
        if (selected + 1 < file_count)
            selected++;

        details = 0;
        return 1;
    }

    if (scancode == SC_ENTER) {
        if (!load_error)
            open_selected();

        return 1;
    }

    if (scancode == SC_BACKSPACE) {
        if (details) {
            details = 0;
        } else if (mode == 0 && cwd != 0) {
            int parent = mios_fs_cd(
                (uint16_t)cwd, ".."
            );

            if (parent >= 0) {
                cwd = parent;
                refresh_files();
            }
        }

        return 1;
    }

    return 0;
}

/* Mostrar hasta tres lineas de texto */
static void draw_preview(void) {
    int x = 31;
    int y = 17;

    for (uint32_t i = 0;
         content[i] && y <= 19;
         i++) {

        char c = content[i];

        if (c == '\n' || x > 73) {
            y++;
            x = 31;

            if (y > 19)
                break;

            if (c == '\n')
                continue;
        }

        if ((unsigned char)c < 32U)
            c = '.';

        putc_at(x++, y, c, COLOR_NORMAL);
    }
}

void gui_files_draw(void) {
    clear_area();

    print_at(
        31, 5,
        "EXPLORADOR DE ARCHIVOS",
        COLOR_NORMAL, 40
    );

    print_at(
        31, 6,
        mode == 0 ? "MIOSFS01" : "BINFS",
        COLOR_NORMAL, 12
    );

    print_at(
        43, 6,
        current_path,
        COLOR_NORMAL, 30
    );

    if (load_error) {
        print_at(
            31, 10,
            "Error al leer el catalogo.",
            COLOR_NORMAL, 40
        );
        print_at(
            31, 12,
            "Comprueba el disco activo.",
            COLOR_NORMAL, 40
        );
    } else if (file_count == 0) {
        print_at(
            31, 10,
            "No hay archivos visibles.",
            COLOR_NORMAL, 40
        );
    } else {
        int first = 0;

        if (selected >= GUI_FILES_VISIBLE)
            first = selected - GUI_FILES_VISIBLE + 1;

        for (int i = 0; i < GUI_FILES_VISIBLE; i++) {
            int index = first + i;

            if (index >= file_count)
                break;

            int y = 8 + i;
            int active = index == selected;

            uint8_t color = active
                ? COLOR_SELECTED
                : COLOR_NORMAL;

            if (active) {
                for (int x = 30; x <= 74; x++)
                    putc_at(x, y, ' ', color);
            }

            print_at(
                31, y,
                active ? ">" : " ",
                color, 1
            );

            print_at(
                33, y,
                files[index].name,
                color, 31
            );

            print_at(
                66, y,
                files[index].is_dir
                    ? "<DIR>" : "FILE",
                color, 6
            );
        }
    }

    print_at(
        31, 15,
        "A:Texto B:BIN R:Refrescar",
        COLOR_NORMAL, 35
    );

    print_at(
        31, 16,
        "Flechas:Elegir Enter:Abrir",
        COLOR_NORMAL, 35
    );

    if (details && selected < file_count) {
        if (mode == 1) {
            print_at(
                31, 17,
                "Tamano (bytes):",
                COLOR_NORMAL, 18
            );
            print_number(
                48, 17,
                files[selected].size,
                COLOR_NORMAL
            );

            print_at(
                31, 18,
                "Sector LBA:",
                COLOR_NORMAL, 18
            );
            print_number(
                44, 18,
                files[selected].lba,
                COLOR_NORMAL
            );

            print_at(
                31, 19,
                "Archivo binario (sin vista texto)",
                COLOR_NORMAL, 40
            );
        } else {
            draw_preview();
        }
    } else {
        print_at(
            31, 18,
            "Backspace: Atras",
            COLOR_NORMAL, 35
        );

        print_at(
            31, 19,
            "Elementos:",
            COLOR_NORMAL, 15
        );

        print_number(
            42, 19,
            (uint32_t)file_count,
            COLOR_NORMAL
        );
    }
}
