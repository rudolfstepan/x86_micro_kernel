#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

extern uint64_t total_memory;

#define MEMORY_STATS_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint64_t detected_usable_bytes;
    uint64_t managed_bytes;
    uint64_t reserved_bytes;
    uint64_t allocated_frame_bytes;
    uint64_t free_frame_bytes;
    uint64_t heap_capacity_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_free_bytes;
    uint64_t heap_largest_free_block;
    uint64_t heap_arena_count;
} memory_stats_t;

void memory_map_reset(void);
int memory_add_usable_region(uint64_t base, uint64_t length);
int memory_reserve_region(uint64_t base, uint64_t length);
int memory_region_is_usable(uint64_t base, uint64_t length);

void k_free(void* ptr);
void* k_malloc(size_t size);
void* k_realloc(void *ptr, size_t new_size);

size_t allocate_frame(void);
size_t allocate_frame_at_or_above(size_t minimum_address);
void free_frame(size_t addr);

void memory_get_stats(memory_stats_t* stats);

int initialize_memory_system(void);
void test_memory(void);

#endif // MEMORY_H
