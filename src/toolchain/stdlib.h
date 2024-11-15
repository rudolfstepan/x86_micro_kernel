#ifndef STDLIB_H    /* This is an "include guard" */
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define SUCCESS 0
#define FAILURE -1

// define syscall indexes
#define SYS_EXIT 0
#define SYS_PRINT 1
#define SYS_DELAY 2
#define SYS_WAIT_ENTER 3



void initialize_memory_system();

void* malloc(size_t size);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);
void* memmove(void* dest, const void* src, size_t n);

int test_memory();

// wrapper functions for system calls
void sleep_ms(uint32_t ms);
void wait_enter_pressed();


void exit(uint8_t status);

#endif /* STDLIB_H */