
#ifndef MIOS_PAGING_H
#define MIOS_PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096U

int paging_init(void);
int paging_ready(void);
uint32_t paging_mapped_mib(void);
int paging_is_mapped(uint32_t address);

/* Solo consulta, no activa la paginacion */
uint32_t paging_directory_address(void);
/* Activar paginacion de la CPU */
int paging_enable(void);

/* Consultar el bit PG del registro CR0 */
int paging_cpu_enabled(void);

/* Direccion virtual reservada para pruebas */
#define MIOS_TEST_VADDR 0x10000000U

/* Asociar y retirar una pagina de prueba */
int paging_test_map(uint32_t physical_frame);
int paging_test_unmap(void);

/* Region experimental para multiples paginas */
#define MIOS_VMM_BASE 0x10400000U
#define MIOS_VMM_PAGES 4U

int vmm_map_page(
    uint32_t index,
    uint32_t physical_frame
);

int vmm_unmap_page(uint32_t index);

int vmm_page_mapped(uint32_t index);



#endif

