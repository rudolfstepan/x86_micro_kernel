#ifndef MEMORY_H    /* This is an "include guard" */
#define MEMORY_H

void initialize_memory_system();

void* malloc(size_t size);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);
void* memset(void* ptr, int value, unsigned int num);
void* memcpy(void* dest, const void* src, unsigned int n);
int memcmp(const void* s1, const void* s2, unsigned int n);

int test_memory();

#endif /* MEMORY_H */