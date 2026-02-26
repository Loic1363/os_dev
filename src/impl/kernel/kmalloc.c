#include "kmalloc.h"

#define KMALLOC_HEAP_SIZE (256 * 1024)
#define KMALLOC_DEFAULT_ALIGN 8

static uint8_t g_kmalloc_heap[KMALLOC_HEAP_SIZE];
static size_t g_kmalloc_used = 0;
static uint64_t g_kmalloc_alloc_count = 0;
static uint64_t g_kmalloc_fail_count = 0;

static size_t align_up(size_t value, size_t align) {
    if (align == 0) {
        align = KMALLOC_DEFAULT_ALIGN;
    }
    if ((align & (align - 1)) != 0) {
        align = KMALLOC_DEFAULT_ALIGN;
    }
    return (value + align - 1) & ~(align - 1);
}

void kmalloc_init() {
    g_kmalloc_used = 0;
    g_kmalloc_alloc_count = 0;
    g_kmalloc_fail_count = 0;
}

void* kmalloc_aligned(size_t size, size_t align) {
    if (size == 0) {
        size = 1;
    }

    size_t start = align_up(g_kmalloc_used, align);
    if (start > KMALLOC_HEAP_SIZE || size > (KMALLOC_HEAP_SIZE - start)) {
        g_kmalloc_fail_count++;
        return 0;
    }

    g_kmalloc_used = start + size;
    g_kmalloc_alloc_count++;
    return &g_kmalloc_heap[start];
}

void* kmalloc(size_t size) {
    return kmalloc_aligned(size, KMALLOC_DEFAULT_ALIGN);
}

void kmalloc_get_stats(struct KmallocStats* out_stats) {
    if (out_stats == 0) {
        return;
    }
    out_stats->heap_size = KMALLOC_HEAP_SIZE;
    out_stats->used = g_kmalloc_used;
    out_stats->free_bytes = KMALLOC_HEAP_SIZE - g_kmalloc_used;
    out_stats->allocations = g_kmalloc_alloc_count;
    out_stats->failed_allocations = g_kmalloc_fail_count;
}
