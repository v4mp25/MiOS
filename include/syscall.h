
#ifndef MIOS_SYSCALL_H
#define MIOS_SYSCALL_H

#include <stdint.h>

#define MIOS_SYS_TEST 1U
#define MIOS_SYS_EXIT 2U
/* Recibe solicitudes desde el ASM */
void syscall_dispatch(uint32_t number);

/* Consultar cantidad de llamadas */
uint32_t syscall_test_count(void);
uint32_t syscall_exit_count(void);
#endif

