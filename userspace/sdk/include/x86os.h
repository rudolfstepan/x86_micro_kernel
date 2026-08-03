#ifndef X86OS_USER_API_H
#define X86OS_USER_API_H

#include <stddef.h>
#include <stdint.h>

enum {
    X86OS_SYS_PUTCHAR = 0,
    X86OS_SYS_PRINT_NUMBER = 1,
    X86OS_SYS_DELAY = 2,
    X86OS_SYS_WAIT_ENTER = 3,
    X86OS_SYS_MALLOC = 4,
    X86OS_SYS_FREE = 5,
    X86OS_SYS_REALLOC = 6,
    X86OS_SYS_GETCHAR = 7,
    X86OS_SYS_EXIT = 9
};

uintptr_t x86os_syscall(uint32_t number, uintptr_t argument1,
                        uintptr_t argument2, uintptr_t argument3);
void x86os_putchar(char value);
void x86os_puts(const char* text);
void x86os_print_number(int value);
void x86os_delay(uint32_t milliseconds);
int x86os_getchar(void);
void* x86os_malloc(size_t size);
void x86os_free(void* pointer);
void* x86os_realloc(void* pointer, size_t size);
void x86os_exit(int status) __attribute__((noreturn));

#endif
