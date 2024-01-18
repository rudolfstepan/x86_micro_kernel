#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdarg.h>

// Type for system call function pointers (must match the kernel's definition)
typedef void (*syscall_func_ptr)(void); // the table of system calls is an array of function pointers
typedef void (*syscall_print_func_ptr)(const char* format, va_list args); // print system call function pointer type


// define the system kernel calls
#define NUM_SYSCALLS 1

// system call numbers
#define SYSCALL_PRINT 0

void initialize_syscall_table();
// the kernel will call this function to register a system call function
void syscall_print(const char* format, va_list args);

// the user code will call this function to make a system call
void sprintf(const char* format, ...);

void vprintf(const char* format, va_list args);

void printf(const char* format, ...);


#endif