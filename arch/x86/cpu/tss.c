/**
 * @file tss.c
 * @brief Task State Segment implementation for privilege switching
 * 
 * The TSS provides the CPU with kernel stack information when transitioning
 * from Ring 3 (user mode) to Ring 0 (kernel mode) during interrupts/syscalls.
 */

#include "arch/x86/include/tss.h"
#include "lib/libc/string.h"
#include <stddef.h>

// Global TSS instance
static tss_entry_t kernel_tss;

/**
 * Initialize the Task State Segment
 */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss) {
    // Zero out entire TSS structure
    memset(&kernel_tss, 0, sizeof(tss_entry_t));
    
    // Set kernel stack for Ring 0 (only fields we use)
    kernel_tss.ss0 = kernel_ss;    // Kernel data segment (0x10)
    kernel_tss.esp0 = kernel_stack; // Top of kernel stack
    
    // Set I/O map base to end of TSS (no I/O permission bitmap)
    kernel_tss.iomap_base = sizeof(tss_entry_t);
    
    // CS and SS are set by the CPU during task switch
    // We don't use hardware task switching, so these remain 0
}

/**
 * Update kernel stack pointer in TSS
 * 
 * This should be called when switching between processes to ensure
 * each process uses its own kernel stack when entering kernel mode.
 */
void tss_set_kernel_stack(uint32_t kernel_stack) {
    kernel_tss.esp0 = kernel_stack;
}

/**
 * Get TSS base address for GDT descriptor
 */
uint32_t tss_get_base(void) {
    return (uint32_t)&kernel_tss;
}

/**
 * Get TSS limit (size - 1) for GDT descriptor
 */
uint32_t tss_get_limit(void) {
    return sizeof(tss_entry_t) - 1;
}
