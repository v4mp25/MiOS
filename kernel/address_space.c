
#include <stdint.h>
#include <stddef.h>

#include "address_space.h"
#include "paging.h"
#include "pmm.h"
#include "irq.h"

#define AS_PRESENT 0x001U
#define AS_WRITE   0x002U

#define AS_KERNEL_TABLES 32U
#define AS_PRIVATE_PDE 66U

typedef struct {
    int used;
    uint32_t directory_frame;
    uint32_t table_frame;
    uint32_t data_frame;
    uint32_t users;
} AddressSpace;

static AddressSpace spaces[MIOS_AS_MAX];

static void clear_page(uint32_t address) {
    uint32_t *p = (uint32_t *)(uintptr_t)address;

    for (uint32_t i = 0; i < 1024; i++) {
        p[i] = 0;
    }
}

static uint32_t read_cr3(void) {
    uint32_t cr3;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    return cr3 & 0xFFFFF000U;
}

void as_init(void) {
    for (uint32_t i = 0; i < MIOS_AS_MAX; i++) {
        spaces[i].used = 0;
        spaces[i].directory_frame = 0;
        spaces[i].table_frame = 0;
        spaces[i].data_frame = 0;
        spaces[i].users = 0;
    }
}

int as_create(void) {
    uint32_t flags = irq_save();

    if (!paging_cpu_enabled() ||
        read_cr3() != paging_directory_address()) {
        irq_restore(flags);
        return -1;
    }

    int slot = -1;

    for (int i = 0; i < MIOS_AS_MAX; i++) {
        if (!spaces[i].used) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }

    uint32_t dir = 0;
    uint32_t table = 0;
    uint32_t data = 0;

    int got_dir = 0;
    int got_table = 0;
    int got_data = 0;
    int data_pinned = 0;

    if (pmm_alloc_frame(&dir) != 0)
        goto fail;
    got_dir = 1;

    if (pmm_alloc_frame(&table) != 0)
        goto fail;
    got_table = 1;

    if (pmm_alloc_frame(&data) != 0)
        goto fail;
    got_data = 1;

    if (pmm_pin_frame(data) != 0)
        goto fail;
    data_pinned = 1;

    /*
     * El kernel actual mapea por identidad
     * los primeros 128 MiB.
     */
    clear_page(dir);
    clear_page(table);
    clear_page(data);

    uint32_t *new_directory =
        (uint32_t *)(uintptr_t)dir;

    uint32_t *private_table =
        (uint32_t *)(uintptr_t)table;

    uint32_t *kernel_directory =
        (uint32_t *)(uintptr_t)
        paging_directory_address();

    /*
     * Compartir las primeras 32 tablas
     * del kernel.
     */
    for (uint32_t i = 0;
         i < AS_KERNEL_TABLES;
         i++) {

        new_directory[i] = kernel_directory[i];
    }

    /*
     * Una pagina privada en 0x10800000.
     * Por ahora solo accesible en Ring 0.
     */
    private_table[0] =
        data | AS_PRESENT | AS_WRITE;

    new_directory[AS_PRIVATE_PDE] =
        table | AS_PRESENT | AS_WRITE;

    spaces[slot].directory_frame = dir;
    spaces[slot].table_frame = table;
    spaces[slot].data_frame = data;
    spaces[slot].users = 0;
    spaces[slot].used = 1;

    irq_restore(flags);

    return slot + 1;

fail:
    if (data_pinned)
        pmm_unpin_frame(data);

    if (got_data)
        pmm_free_frame(data);

    if (got_table)
        pmm_free_frame(table);

    if (got_dir)
        pmm_free_frame(dir);

    irq_restore(flags);
    return -1;
}


int as_destroy(uint32_t id) {
    uint32_t flags = irq_save();

    /* Verificar que el ID sea valido */
    if (id == 0 || id > MIOS_AS_MAX) {
        irq_restore(flags);
        return -1;
    }

    AddressSpace *s = &spaces[id - 1];

    /* Comprobar que el espacio existe */
    if (!s->used) {
        irq_restore(flags);
        return -1;
    }

    /*
     * No destruir espacios que todavia
     * estan asociados a procesos o hilos.
     */
    if (s->users != 0) {
        irq_restore(flags);
        return -2;
    }

    /*
     * Nunca destruir el directorio
     * que esta utilizando la CPU.
     */
    if (read_cr3() == s->directory_frame) {
        irq_restore(flags);
        return -2;
    }

    /*
     * La destruccion solo se permite
     * desde el espacio principal del kernel.
     */
    if (read_cr3() != paging_directory_address()) {
        irq_restore(flags);
        return -2;
    }

    /*
     * La pagina privada debe tener exactamente
     * una referencia registrada.
     */
    if (pmm_frame_pin_count(s->data_frame) != 1) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Retirar la referencia de la pagina privada.
     */
    if (pmm_unpin_frame(s->data_frame) != 0) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Liberar los tres marcos fisicos:
     * pagina privada, tabla y directorio.
     */
    int a = pmm_free_frame(s->data_frame);
    int b = pmm_free_frame(s->table_frame);
    int c = pmm_free_frame(s->directory_frame);

    if (a != 0 || b != 0 || c != 0) {
        /*
         * Conservar el descriptor si ocurre
         * un error durante la liberacion.
         */
        irq_restore(flags);
        return -1;
    }

    /* Limpiar completamente el descriptor */
    s->used = 0;
    s->directory_frame = 0;
    s->table_frame = 0;
    s->data_frame = 0;
    s->users = 0;

    irq_restore(flags);
    return 0;
}


int as_get(
    uint32_t id,
    uint32_t *cr3,
    uint32_t *private_frame
) {
    uint32_t flags = irq_save();

    if (id == 0 || id > MIOS_AS_MAX ||
        !spaces[id - 1].used ||
        cr3 == NULL ||
        private_frame == NULL) {

        irq_restore(flags);
        return -1;
    }

    *cr3 = spaces[id - 1].directory_frame;
    *private_frame = spaces[id - 1].data_frame;

    irq_restore(flags);
    return 0;
}

uint32_t as_count(void) {
    uint32_t flags = irq_save();
    uint32_t count = 0;

    for (uint32_t i = 0; i < MIOS_AS_MAX; i++) {
        if (spaces[i].used)
            count++;
    }

    irq_restore(flags);
    return count;
}

/* Registrar un usuario del espacio */
int as_acquire(uint32_t id) {
    uint32_t flags = irq_save();

    if (id == 0 || id > MIOS_AS_MAX) {
        irq_restore(flags);
        return -1;
    }

    AddressSpace *s = &spaces[id - 1];

    if (!s->used || s->users == UINT32_MAX) {
        irq_restore(flags);
        return -1;
    }

    s->users++;

    irq_restore(flags);
    return 0;
}

/* Retirar un usuario del espacio */
int as_release(uint32_t id) {
    uint32_t flags = irq_save();

    if (id == 0 || id > MIOS_AS_MAX) {
        irq_restore(flags);
        return -1;
    }

    AddressSpace *s = &spaces[id - 1];

    if (!s->used || s->users == 0) {
        irq_restore(flags);
        return -1;
    }

    s->users--;

    irq_restore(flags);
    return 0;
}

/* Consultar el numero de usuarios */
int as_users(uint32_t id) {
    uint32_t flags = irq_save();

    if (id == 0 || id > MIOS_AS_MAX ||
        !spaces[id - 1].used) {
        irq_restore(flags);
        return -1;
    }

    int result = (int)spaces[id - 1].users;

    irq_restore(flags);
    return result;
}
