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

// Macros for try-catch handling
#define try(ctx) if (setjmp(&(ctx)) == 0)
#define catch(ctx, ex) else if ((ctx).exception_code == (ex))

#pragma pack(push, 1)
typedef struct TryContext {
    uint32_t esp;    // Saved ESP
    uint32_t ebp;    // Saved EBP
    uint32_t eip;    // Saved EIP (return address)
    int exception_code; // Exception code
} TryContext;
#pragma pack(pop)

extern TryContext* current_try_context;

// Declare setjmp and longjmp
extern int setjmp(TryContext* ctx);
extern void longjmp(TryContext* ctx, int exception_code);

void throw(TryContext* ctx, int exception_code);

void initialize_memory_system();

void* malloc(size_t size);
void* realloc(void *ptr, size_t new_size);
void free(void* ptr);
void secure_free(void *ptr, size_t size);
void* memmove(void* dest, const void* src, size_t n);

int test_memory();

void sys_call(int syscall_index, int parameter1, int parameter2, int parameter3);
// wrapper functions for system calls
void sleep_ms(uint32_t ms);
void wait_enter_pressed();

void exit(uint8_t status);

#endif /* STDLIB_H */