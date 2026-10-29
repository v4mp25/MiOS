
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
#endif

