/**
 * @file syscall_table.c
 * @brief System call table and handler implementation
 * 
 * This module provides the kernel's system call interface, allowing
 * user programs to request kernel services via INT 0x80.
 */

#include <stdbool.h>
#include "arch/x86/sys.h"
#include "drivers/video/display.h"
#include "drivers/char/kb.h"
#include "kernel/time/pit.h"
#include "mm/kmalloc.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"  // For SYS_MALLOC, SYS_FREE, SYS_REALLOC, etc.

//---------------------------------------------------------------------------------------------
// Syscall Entry Points
//---------------------------------------------------------------------------------------------

/**
 * Simple kernel greeting function for syscall testing
 */
void kernel_hello(void) {
    printf("Hello from the kernel. All engines running.\n");
}

/**
 * Print a number from userspace for syscall testing
 */
void kernel_print_number(int number) {
    printf("Kernel received number: %d\n", number);
}

//---------------------------------------------------------------------------------------------
// System Call Table
//---------------------------------------------------------------------------------------------

/**
 * Global syscall dispatch table - maps syscall numbers to function pointers
 * User programs trigger INT 0x80 with syscall number in EAX
 */
void* syscall_table[512] __attribute__((section(".syscall_table"))) = {
    (void*)&display_putchar,            // Syscall 0: Write character to display
    (void*)&kernel_print_number,        // Syscall 1: Print number (for testing)
    (void*)&pit_delay,                  // Syscall 2: Millisecond delay
    (void*)&kb_wait_enter,              // Syscall 3: Wait for Enter key
    (void*)&k_malloc,                   // Syscall 4: Allocate memory
    (void*)&k_free,                     // Syscall 5: Free memory
    (void*)&k_realloc,                  // Syscall 6: Reallocate memory
    (void*)&getchar,                    // Syscall 7: Read character from keyboard
    (void*)&register_interrupt_handler, // Syscall 8: Register IRQ handler
    // Add more syscalls here as needed
};

//---------------------------------------------------------------------------------------------
// System Call Handler
//---------------------------------------------------------------------------------------------

/**
 * Main syscall dispatcher - called from INT 0x80 handler in arch/x86/cpu/syscall.asm
 * 
 * Retrieves syscall number and arguments from CPU registers:
 * - EAX: syscall number
 * - EBX: argument 1
 * - ECX: argument 2
 * - EDX: argument 3
 * 
 * @param irq_number Unused, for compatibility with IRQ handler signature
 */
void syscall_handler(void* irq_number) {
    int syscall_index, arg1, arg2, arg3;
    
    // Retrieve register values into C variables
    __asm__ __volatile__(
        "" // No instructions needed, just read registers
        : "=a"(syscall_index),  // EAX -> syscall_index
          "=b"(arg1),           // EBX -> arg1
          "=c"(arg2),           // ECX -> arg2
          "=d"(arg3)            // EDX -> arg3
        :                       // No input operands
    );

    // Validate syscall index
    if (syscall_index < 0 || syscall_index >= 512 || syscall_table[syscall_index] == 0) {
        printf("Invalid syscall index: %d\n", syscall_index);
        return;
    }

    // Retrieve function pointer from table
    void* func_ptr = (void*)syscall_table[syscall_index];

    // Dispatch based on syscall number and argument count
    switch (syscall_index) {
        // No-argument syscalls
        case 0:  // kernel_hello
        case 3:  // kb_wait_enter
        case SYS_FREE:  // k_free
            ((void (*)(void))func_ptr)();
            break;

        // Single-argument syscalls
        case 1:  // kernel_print_number
        case 2:  // pit_delay
        case SYS_MALLOC:  // k_malloc
            ((void (*)(int))func_ptr)((uint32_t)arg1);
            break;

        // Two-argument syscalls
        case SYS_REALLOC:  // k_realloc
            ((void* (*)(void*, size_t))func_ptr)((void*)arg1, (size_t)arg2);
            break;

        case SYS_INSTALL_IRQ:  // register_interrupt_handler
            ((void (*)(int, void*))func_ptr)(arg1, (void*)arg2);
            break;

        // Syscalls with return values
        case SYS_TERMINAL_GETCHAR:  // getchar
            ((void* (*)(void))func_ptr)();
            break;

        default:
            printf("Unknown syscall index: %d\n", syscall_index);
            break;
    }
}
