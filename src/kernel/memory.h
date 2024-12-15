#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

void initialize_memory_system();
void show_memory_map();
void k_free(void* ptr);
void* k_malloc(size_t size);
void* k_realloc(void *ptr, size_t new_size);

void test_memory();

#endif // MEMORY_H