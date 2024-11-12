#ifndef SYSTEM_H
#define SYSTEM_H


#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define NUM_SYSCALLS 1

// system call numbers
// #define SYSCALL_PRINT 0
// #define SYSCALL_MALLOC 1
// #define SYSCALL_FREE 2

#define SYSCALL_SLEEP 0

// for memory dump
#define BYTES_PER_LINE 16
#define MAX_LINES 20
#define MULTIBOOT_TAG_TYPE_MMAP 6

// syscalls
void initialize_syscall_table();
void syscall_sleep(int ticks);

// utility functions
void memory_dump(uint32_t start_address, uint32_t end_address);
//void print_memory_map(const MultibootInfo* mb_info);

// Function to disable interrupts (implementation may vary by architecture)
void disable_interrupts();
// Function to enable interrupts (implementation may vary by architecture)
void enable_interrupts();

#endif