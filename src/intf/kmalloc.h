#pragma once

#include <stddef.h>
#include <stdint.h>

struct KmallocStats {
    uint64_t heap_size;
    uint64_t used;
    uint64_t free_bytes;
    uint64_t allocations;
    uint64_t failed_allocations;
};

void kmalloc_init();
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t align);
void kmalloc_get_stats(struct KmallocStats* out_stats);
