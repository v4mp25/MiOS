/* Phase 4.2 experimental heap for a future freestanding Mbed TLS port.
 * 2 MiB, single-thread only, no libc, 16-byte aligned allocation.
 * Do not install as a global malloc replacement until linking is reviewed.
 */
#ifndef MIOS_TLS4_HEAP_INC
#define MIOS_TLS4_HEAP_INC
#define TLS4_HEAP_BYTES (2U * 1024U * 1024U)

typedef struct tls4_heap_block {
    uint32_t size;
    uint32_t used;
    struct tls4_heap_block *next;
    struct tls4_heap_block *prev;
} tls4_heap_block;
static uint8_t tls4_heap_arena[TLS4_HEAP_BYTES]
    __attribute__((aligned(16)));
static tls4_heap_block *tls4_heap_head;

static void tls4_heap_init(void)
{
    tls4_heap_head = (tls4_heap_block *)(void *)tls4_heap_arena;
    tls4_heap_head->size = TLS4_HEAP_BYTES - (uint32_t)sizeof(tls4_heap_block);
    tls4_heap_head->used = 0U;
    tls4_heap_head->next = 0;
    tls4_heap_head->prev = 0;
}
static void *tls4_calloc(uint32_t nmemb, uint32_t size)
{
    if (!nmemb || !size || nmemb > 0xFFFFFFFFU/size) return 0;
    uint32_t n = nmemb * size;
    if (n > 0xFFFFFFEFU) return 0;
    n = (n + 15U) & ~15U;
    if (!tls4_heap_head) tls4_heap_init();
    tls4_heap_block *p = tls4_heap_head;
    for (; p; p = p->next) {
        if (p->used || p->size < n) continue;
        if (p->size >= n + sizeof(tls4_heap_block) + 16U) {
            tls4_heap_block *rest = (tls4_heap_block *)(void *)
                ((uint8_t *)(p + 1) + n);
            rest->size = p->size - n - (uint32_t)sizeof(tls4_heap_block);
            rest->used = 0U;
            rest->next = p->next;
            rest->prev = p;
            if (rest->next) rest->next->prev = rest;
            p->next = rest;
            p->size = n;
        }
        p->used = 1U;
        uint8_t *out = (uint8_t *)(void *)(p + 1);
        for (uint32_t i = 0U; i < n; i++) out[i] = 0U;
        return out;
    }
    return 0;
}
static void tls4_heap_coalesce(tls4_heap_block *p)
{
    if (p->next && !p->next->used) {
        tls4_heap_block *n = p->next;
        p->size += (uint32_t)sizeof(tls4_heap_block) + n->size;
        p->next = n->next;
        if (p->next) p->next->prev = p;
    }
}
static void tls4_free(void *ptr)
{
    if (!ptr || !tls4_heap_head) return;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t low = (uintptr_t)tls4_heap_arena;
    uintptr_t high = low + TLS4_HEAP_BYTES;
    if (addr < low + sizeof(tls4_heap_block) || addr >= high) return;
    tls4_heap_block *p = (tls4_heap_block *)ptr - 1;
    /* Only free blocks that are currently linked to our arena. */
    tls4_heap_block *cur = tls4_heap_head;
    while (cur && cur != p) cur = cur->next;
    if (!cur || !cur->used) return;
    cur->used = 0U;
    tls4_heap_coalesce(cur);
    if (cur->prev && !cur->prev->used) tls4_heap_coalesce(cur->prev);
}
static int tls4_heap_selftest(void)
{
    tls4_heap_init();
    uint8_t *a = tls4_calloc(64U, 1U);
    uint8_t *b = tls4_calloc(128U, 1U);
    if (!a || !b || a == b) return -1;
    for (uint32_t i=0U;i<64U;i++) if (a[i]!=0U) return -1;
    a[0]=0xAAU; b[0]=0x55U;
    tls4_free(a); tls4_free(b);
    uint8_t *c=tls4_calloc(16384U,1U);
    if (!c || c[0]!=0U) return -1;
    tls4_free(c);
    return 0;
}
#endif
