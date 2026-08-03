#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_PROGRAM_REGION_START 0x02100000U
#define KERNEL_PROGRAM_REGION_SIZE  (8U * 1024U * 1024U)

extern uint64_t total_memory;

void memory_map_reset(void);
int memory_add_usable_region(uint64_t base, uint64_t length);
int memory_reserve_region(uint64_t base, uint64_t length);
int memory_region_is_usable(uint64_t base, uint64_t length);

void k_free(void* ptr);
void* k_malloc(size_t size);
void* k_realloc(void *ptr, size_t new_size);

size_t allocate_frame(void);
void free_frame(size_t addr);

int initialize_memory_system(void);
void test_memory(void);

#endif // MEMORY_H
