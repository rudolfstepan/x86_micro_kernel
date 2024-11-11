#ifndef STDLIB_H    /* This is an "include guard" */
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define SUCCESS 0
#define FAILURE -1


void initialize_memory_system();

void* malloc(size_t size);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);


int test_memory();
void sleep_ms(unsigned int milliseconds);
void* memmove(void* dest, const void* src, size_t n);

void exit(uint8_t status);

#endif /* STDLIB_H */