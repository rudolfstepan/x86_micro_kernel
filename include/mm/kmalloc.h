#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

extern size_t total_memory;

void k_free(void* ptr);
void* k_malloc(size_t size);
void* k_realloc(void *ptr, size_t new_size);

void test_memory();

#endif // MEMORY_H