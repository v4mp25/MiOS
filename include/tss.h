
#ifndef MIOS_TSS_H
#define MIOS_TSS_H

#include <stdint.h>

void tss_prepare(void);
void tss_load(void);

uint32_t tss_get_base(void);
uint32_t tss_get_size(void);

int tss_verify(void);

/* Para futuras transiciones Ring 3 -> Ring 0 */
void tss_set_kernel_stack(uint32_t esp0);

#endif
