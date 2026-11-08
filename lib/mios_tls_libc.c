/* MiOS TLS 4.3: minimal freestanding libc subset.
 * Requirements: byte-addressable memory, single-thread safety for any
 * caller-shared buffers. This file does not implement printf or entropy.
 * Compile with -fno-builtin to prevent compiler-introduced libc calls.
 */
#include "mios_tls_libc.h"
#include <stdint.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (n == 0 || d == s) return dst;
    /* Integer representation avoids undefined ordering comparisons of
     * pointers into unrelated objects. The subtraction test only matters
     * when d's address is at or after s's. */
    if ((uintptr_t)d > (uintptr_t)s &&
        ((uintptr_t)d - (uintptr_t)s) < n) {
        for (size_t i = n; i != 0; --i) d[i - 1] = s[i - 1];
    } else {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; ++i)
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    for (;;) {
        const unsigned char x = (unsigned char)*a++;
        const unsigned char y = (unsigned char)*b++;
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = (unsigned char)a[i];
        const unsigned char y = (unsigned char)b[i];
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    const char wanted = (char)c;
    for (;;) {
        if (*s == wanted) return (char *)s;
        if (*s == '\0') return NULL;
        ++s;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    const char wanted = (char)c;
    for (;;) {
        if (*s == wanted) last = s;
        if (*s == '\0') return (char *)last;
        ++s;
    }
}

char *strcpy(char *restrict d, const char *restrict s)
{
    char *start = d;
    while ((*d++ = *s++) != '\0') {}
    return start;
}

char *strncpy(char *restrict d, const char *restrict s, size_t n)
{
    char *start = d;
    size_t i = 0;
    for (; i < n && s[i] != '\0'; ++i) d[i] = s[i];
    for (; i < n; ++i) d[i] = '\0';
    return start;
}

char *strcat(char *restrict d, const char *restrict s)
{
    char *start = d;
    while (*d) ++d;
    while ((*d++ = *s++) != '\0') {}
    return start;
}

char *strncat(char *restrict d, const char *restrict s, size_t n)
{
    char *start = d;
    while (*d) ++d;
    size_t i = 0;
    for (; i < n && s[i] != '\0'; ++i) d[i] = s[i];
    d[i] = '\0';
    return start;
}

char *strstr(const char *h, const char *needle)
{
    if (!*needle) return (char *)h;
    for (; *h; ++h) {
        const char *p = h;
        const char *q = needle;
        while (*p && *q && *p == *q) { ++p; ++q; }
        if (!*q) return (char *)h;
    }
    return NULL;
}
