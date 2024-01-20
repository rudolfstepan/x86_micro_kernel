#ifndef SYSTEM_H
#define SYSTEM_H


#include <stdarg.h>
#include <stddef.h>

#define NUM_SYSCALLS 1

// system call numbers
#define SYSCALL_PRINT 0
#define SYSCALL_MALLOC 1
#define SYSCALL_FREE 2


// // Type for system call function pointers (must match the kernel's definition)
// typedef void (*syscall_func_ptr)(void); // the table of system calls is an array of function pointers
// typedef void (*syscall_print_func_ptr)(const char* format, va_list args); // print system call function pointer type
// typedef void* (*syscall_malloc_func_ptr)(size_t size); // system call function pointer type
// typedef void (*syscall_free_func_ptr)(void* ptr); // system call function pointer type


// void initialize_syscall_table();

// // define the system kernel calls
// // the kernel will call this function to register a system call function
// void syscall_print(const char* format, va_list args);
// void* syscall_malloc(size_t size);
// void syscall_free(void* ptr);


// // the user code will call this function to make a system call
// void sprintf(const char* format, ...);
// // void vprintf(const char* format, va_list args);
// // void printf(const char* format, ...);

// void* smalloc(size_t size);
// void sfree(void* ptr);

#endif