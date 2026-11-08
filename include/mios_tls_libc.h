#ifndef MIOS_TLS_LIBC_H
#define MIOS_TLS_LIBC_H
/* Small, freestanding C runtime subset for MiOS TLS integration.
 * No allocator, clock, cryptographic RNG or printf is implemented here.
 * These functions are standard C-compatible; do NOT combine with another
 * object exporting the same symbols. */
#include <stddef.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strcpy(char *restrict dest, const char *restrict src);
char *strncpy(char *restrict dest, const char *restrict src, size_t n);
char *strcat(char *restrict dest, const char *restrict src);
char *strncat(char *restrict dest, const char *restrict src, size_t n);
char *strstr(const char *haystack, const char *needle);

#endif
