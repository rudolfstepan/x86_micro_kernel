/**
 * @file panic.c
 * @brief Kernel panic handler implementation
 */

#include "include/kernel/panic.h"
#include "arch/x86/include/interrupt.h"
#include "lib/libc/stdio.h"
#include "drivers/video/video.h"

// Prevent recursive panics
static int panic_in_progress = 0;

/**
 * Halt the CPU forever
 */
static void halt(void) {
    cpu_halt_forever();
}

/**
 * Kernel panic - unrecoverable error
 */
void __attribute__((noreturn)) panic(const char* message) {
    // Disable interrupts immediately
    irq_disable();
    
    // Check for recursive panic
    if (panic_in_progress) {
        halt();
    }
    panic_in_progress = 1;
    
    // Clear screen and use error colors (white on red)
    set_color((RED << 4) | WHITE);
    clear_screen();
    
    printf("\n");
    printf("================================================================================\n");
    printf("                          *** KERNEL PANIC ***                                 \n");
    printf("================================================================================\n");
    printf("\n");
    printf("An unrecoverable error has occurred. The system has been halted.\n");
    printf("\n");
    printf("Error: %s\n", message ? message : "Unknown error");
    printf("\n");
    
    // TODO: Dump CPU registers if we have them
    // TODO: Dump stack trace
    // TODO: Dump memory map
    
    printf("================================================================================\n");
    printf("System Halted. Please restart your computer.\n");
    printf("================================================================================\n");
    
    halt();
}

/**
 * Kernel assertion failure
 */
void __attribute__((noreturn)) kassert_fail(const char* expr, const char* file,
                                              int line, const char* func) {
    // Disable interrupts immediately
    irq_disable();
    
    // Check for recursive panic
    if (panic_in_progress) {
        halt();
    }
    panic_in_progress = 1;
    
    // Clear screen and use error colors (white on red)
    set_color((RED << 4) | WHITE);
    clear_screen();
    
    printf("\n");
    printf("================================================================================\n");
    printf("                       *** ASSERTION FAILED ***                                \n");
    printf("================================================================================\n");
    printf("\n");
    printf("Assertion:  %s\n", expr ? expr : "Unknown");
    printf("File:       %s\n", file ? file : "Unknown");
    printf("Line:       %d\n", line);
    printf("Function:   %s\n", func ? func : "Unknown");
    printf("\n");
    printf("The kernel has detected an internal consistency error and cannot continue.\n");
    printf("\n");
    printf("================================================================================\n");
    printf("System Halted. Please restart your computer.\n");
    printf("================================================================================\n");
    
    halt();
}
