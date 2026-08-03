/**
 * @file syscall_table.c
 * @brief System call table and handler implementation
 * 
 * This module provides the kernel's system call interface, allowing
 * user programs to request kernel services via INT 0x80.
 */

#include <stdbool.h>
#include "arch/x86/include/sys.h"
#include "drivers/video/display.h"
#include "drivers/char/kb.h"
#include "kernel/time/pit.h"
#include "kernel/sched/scheduler.h"
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
    NULL,                               // Syscall 8: reserved (IRQ registration is privileged)
    (void*)&task_exit,                  // Syscall 9: Terminate current task
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
 * @param regs Saved register frame built by syscall_handler_asm
 */
void syscall_handler(Registers* regs) {
    const uint32_t syscall_index = regs->eax;
    const uint32_t arg1 = regs->ebx;
    const uint32_t arg2 = regs->ecx;
    uint32_t result = 0;

    // Validate syscall index
    if (syscall_index >= 512 || syscall_table[syscall_index] == 0) {
        printf("Invalid syscall index: %u\n", syscall_index);
        regs->eax = (uint32_t)-1;
        return;
    }

    switch (syscall_index) {
        case SYS_TERMINAL_PUTCHAR:
            display_putchar((char)arg1);
            break;
        case SYS_PRINT:
            kernel_print_number((int)arg1);
            break;
        case SYS_DELAY:
            pit_delay(arg1);
            break;
        case SYS_WAIT_ENTER:
            kb_wait_enter();
            break;
        case SYS_MALLOC:
            result = (uint32_t)(uintptr_t)k_malloc((size_t)arg1);
            break;
        case SYS_FREE:
            k_free((void*)(uintptr_t)arg1);
            break;
        case SYS_REALLOC:
            result = (uint32_t)(uintptr_t)k_realloc((void*)(uintptr_t)arg1,
                                                    (size_t)arg2);
            break;
        case SYS_TERMINAL_GETCHAR:
            result = (uint32_t)(uint8_t)getchar();
            break;
        case SYS_EXIT:
            task_exit();
        default:
            result = (uint32_t)-1;
            break;
    }

    regs->eax = result;
}
