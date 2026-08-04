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
#include "drivers/char/rtc.h"
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

static uint32_t syscall_get_date(void) {
    int year, month, day;
    read_date(&year, &month, &day);
    return ((uint32_t)year << 16) | ((uint32_t)month << 8) | (uint32_t)day;
}

static uint32_t syscall_get_time(void) {
    int hours, minutes, seconds;
    read_time(&hours, &minutes, &seconds);
    return ((uint32_t)hours << 16) | ((uint32_t)minutes << 8) |
           (uint32_t)seconds;
}

static uint32_t syscall_memory_kb(void) {
    uint64_t kibibytes = total_memory / 1024U;
    return kibibytes > UINT32_MAX ? UINT32_MAX : (uint32_t)kibibytes;
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
    (void*)&process_user_malloc,        // Syscall 4: Process-local allocation
    (void*)&process_user_free,          // Syscall 5: Release user allocation
    (void*)&process_user_realloc,       // Syscall 6: Resize user allocation
    (void*)&getchar,                    // Syscall 7: Read character from keyboard
    NULL,                               // Syscall 8: reserved (IRQ registration is privileged)
    (void*)&task_exit,                  // Syscall 9: Terminate current task
    (void*)&syscall_get_date,           // Syscall 10: Packed RTC date
    (void*)&syscall_get_time,           // Syscall 11: Packed RTC time
    (void*)&pit_ticks,                  // Syscall 12: Milliseconds since boot
    (void*)&syscall_memory_kb,          // Syscall 13: Usable memory in KiB
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
            result = (uint32_t)(uintptr_t)process_user_malloc((size_t)arg1);
            if (result == 0) result = (uint32_t)-12; /* ENOMEM */
            break;
        case SYS_FREE:
            result = process_user_free((void*)(uintptr_t)arg1) == 0
                ? 0U : (uint32_t)-22; /* EINVAL */
            break;
        case SYS_REALLOC:
            result = (uint32_t)(uintptr_t)process_user_realloc(
                (void*)(uintptr_t)arg1, (size_t)arg2);
            if (result == 0 && arg2 != 0) result = (uint32_t)-12;
            break;
        case SYS_TERMINAL_GETCHAR:
            result = (uint32_t)(uint8_t)getchar();
            break;
        case SYS_EXIT:
            task_exit();
        case SYS_GET_DATE:
            result = syscall_get_date();
            break;
        case SYS_GET_TIME:
            result = syscall_get_time();
            break;
        case SYS_UPTIME_MS:
            result = pit_ticks();
            break;
        case SYS_MEMORY_KB:
            result = syscall_memory_kb();
            break;
        default:
            result = (uint32_t)-1;
            break;
    }

    regs->eax = result;
}
