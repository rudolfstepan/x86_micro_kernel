#ifndef STDLIB_H    /* This is an "include guard" */
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define SUCCESS 0
#define FAILURE -1

// define syscall indexes
#define SYS_TERMINAL_PUTCHAR 0 // One argument syscall
#define SYS_PRINT 1
#define SYS_DELAY 2
#define SYS_WAIT_ENTER 3
#define SYS_MALLOC 4
#define SYS_FREE 5
#define SYS_REALLOC 6
#define SYS_TERMINAL_GETCHAR 7
#define SYS_INSTALL_IRQ 8

// // Macros for try-catch handling
// #define try(ctx) if (setjmp(&(ctx)) == 0)
// #define catch(ctx, ex) else if ((ctx).exception_code == (ex))


// typedef struct TryContext {
//     //uint32_t padding1;  // Padding before the structure
//     uint32_t esp;
//     uint32_t ebp;
//     uint32_t eip;
//     int exception_code;
//     //uint32_t padding2;  // Padding after the structure
// } TryContext;

// extern TryContext* current_try_context;

uint32_t get_esp();
uint32_t get_ebp();

// Declare setjmp and longjmp
// extern int setjmp(TryContext* ctx);
// extern void longjmp(TryContext* ctx, int exception_code);

// void throw(TryContext* ctx, int exception_code);

void initialize_memory_system();

void* malloc(size_t size);
void* aligned_alloc(size_t alignment, size_t size);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);
void* memmove(void* dest, const void* src, size_t n);

// wrapper functions for system calls
void delay_ms(uint32_t ms);
void wait_enter_pressed();

void exit(uint8_t status);

// Disable interrupts
void disable_interrupts();

// Enable interrupts
void enable_interrupts();

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);
uint64_t __umoddi3(uint64_t dividend, uint64_t divisor);

#endif /* STDLIB_H */