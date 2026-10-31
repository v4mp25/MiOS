
#include <stdint.h>
#include <stddef.h>

#include "elf32.h"
#include "address_space.h"
#include "paging.h"
#include "irq.h"

#define ELF_PAGE_SIZE 4096U

/*
 * Cargador ELF32 inicial de MiOS.
 *
 * Se ejecuta desde Ring 0, sobre la
 * identidad fisica del kernel.
 *
 * La funcion solo puede cargar dentro
 * de la pagina privada de un espacio
 * de direcciones existente.
 */

int elf32_load(
    const void *image,
    uint32_t size,
    uint32_t address_space_id,
    uint32_t *entry
) {
    if (image == NULL || entry == NULL)
        return -1;

    /*
     * Primero validar toda la estructura
     * y los segmentos, antes de escribir
     * en la memoria del proceso.
     */
    Elf32_LoadInfo info;

    if (elf32_validate_segments(
            image, size, &info
        ) != 0)
        return -1;

    Elf32_Ehdr header;

    if (elf32_validate(
            image, size, &header
        ) != 0)
        return -1;

    uint32_t flags = irq_save();

    if (!paging_cpu_enabled()) {
        irq_restore(flags);
        return -1;
    }

    /*
     * Consultar el marco que pertenece
     * al espacio de usuario.
     */
    uint32_t cr3 = 0;
    uint32_t physical_frame = 0;

    if (as_get(
            address_space_id,
            &cr3,
            &physical_frame
        ) != 0 ||
        cr3 == 0U ||
        physical_frame == 0U) {

        irq_restore(flags);
        return -1;
    }

    /*
     * Esta etapa utiliza el mapeo
     * de identidad de 128 MiB de MiOS.
     */
    if ((physical_frame & 0xFFFU) != 0U ||
        physical_frame >
            0x08000000U - ELF_PAGE_SIZE) {

        irq_restore(flags);
        return -1;
    }

    uint8_t *destination =
        (uint8_t *)(uintptr_t)physical_frame;

    const uint8_t *source =
        (const uint8_t *)image;

    /*
     * Limpiar la pagina de codigo/datos.
     * La pila USER no se modifica.
     */
    for (uint32_t i = 0;
         i < ELF_PAGE_SIZE;
         i++) {

        destination[i] = 0;
    }

    /*
     * Copiar cada segmento PT_LOAD
     * a su desplazamiento virtual dentro
     * de la pagina privada.
     */
    for (uint32_t i = 0;
         i < header.e_phnum;
         i++) {

        Elf32_Phdr segment;

        uint32_t offset =
            header.e_phoff +
            i * sizeof(Elf32_Phdr);

        const uint8_t *segment_bytes =
            source + offset;

        uint8_t *header_bytes =
            (uint8_t *)&segment;

        for (uint32_t j = 0;
             j < sizeof(Elf32_Phdr);
             j++) {

            header_bytes[j] =
                segment_bytes[j];
        }

        if (segment.p_type != PT_LOAD)
            continue;

        /*
         * elf32_validate_segments ya
         * garantizo que p_vaddr y p_memsz
         * caben en la pagina autorizada.
         */
        uint32_t destination_offset =
            segment.p_vaddr -
            MIOS_ELF_USER_BASE;

        for (uint32_t j = 0;
             j < segment.p_filesz;
             j++) {

            destination[destination_offset + j] =
                source[segment.p_offset + j];
        }

        /*
         * Inicializar con ceros la memoria
         * adicional de tipo BSS.
         */
        for (uint32_t j = segment.p_filesz;
             j < segment.p_memsz;
             j++) {

            destination[destination_offset + j] = 0;
        }
    }

    *entry = info.entry;

    irq_restore(flags);
    return 0;
}
