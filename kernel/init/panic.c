/**
 * @file panic.c
 * @brief Kernel panic handler implementation
 */

#include "include/kernel/panic.h"
#include "arch/x86/include/interrupt.h"
#include "lib/libc/stdio.h"
#include "drivers/video/display.h"

// Prevent recursive panics
static int panic_in_progress = 0;

/**
 * Halt the CPU forever
 */
static void halt(void) __attribute__((noreturn));
static void halt(void) {
    cpu_halt_forever();
    __builtin_unreachable();
}

static void panic_rule(void) {
    display_set_color(LIGHT_RED);
    printf("-------------------------------------------------------------------------------\n");
}

static void panic_header(const char *title) {
    display_set_color(WHITE);
    display_clear();
    panic_rule();
    display_set_color(LIGHT_RED);
    printf("                            %s\n", title);
    panic_rule();
    printf("\n");
    display_set_color(WHITE);
    printf("  The operating system encountered a fatal error and cannot continue.\n");
    printf("  Your files on disk have not been modified by this panic handler.\n\n");
}

static void panic_label(const char *label) {
    display_set_color(LIGHT_RED);
    printf("  %s\n", label);
    display_set_color(WHITE);
}

static void panic_footer(void) {
    printf("\n");
    panic_rule();
    display_set_color(YELLOW);
    printf("  ACTION REQUIRED\n");
    display_set_color(WHITE);
    printf("  Restart the computer. If this error repeats, record the message above.\n");
    panic_rule();
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
    
    panic_header("KERNEL PANIC");
    panic_label("ERROR");
    printf("  %s\n", message ? message : "Unknown kernel error");
    panic_footer();
    
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
    
    panic_header("KERNEL ASSERTION FAILED");
    panic_label("FAILED CHECK");
    printf("  Expression : %s\n", expr ? expr : "Unknown");
    printf("  Source     : %s:%d\n", file ? file : "Unknown", line);
    printf("  Function   : %s\n", func ? func : "Unknown");
    panic_footer();
    
    halt();
}
