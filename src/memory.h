#ifndef MEMORY_H    /* This is an "include guard" */
#define MEMORY_H

void initializeHeap();

void* memset(void* ptr, int value, unsigned int num);
void* malloc(unsigned int size);
void free(void *ptr);
void* memcpy(void* dest, const void* src, unsigned int n);

int memcmp(const void* s1, const void* s2, unsigned int n);

int test_memory();

#endif /* MEMORY_H */