
#ifndef MIOS_GDT_H
#define MIOS_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08U
#define GDT_KERNEL_DATA 0x10U
#define GDT_USER_CODE   0x1BU
#define GDT_USER_DATA   0x23U

void gdt_init(void);
int gdt_verify(void);
#define GDT_TSS_SELECTOR 0x28U
#endif
