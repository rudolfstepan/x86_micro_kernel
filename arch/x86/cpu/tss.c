/**
 * @file tss.c
 * @brief Task State Segment implementation for privilege switching
 * 
 * The TSS provides the CPU with kernel stack information when transitioning
 * from Ring 3 (user mode) to Ring 0 (kernel mode) during interrupts/syscalls.
 */

#include "arch/x86/include/tss.h"
#include "arch/x86/mm/paging.h"
#include "include/kernel/fatal.h"
#include "lib/libc/string.h"
#include <stddef.h>

// Global TSS instance
static tss_entry_t kernel_tss;
static tss_entry_t double_fault_tss;
static uint8_t double_fault_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

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

    /* Vector 8 uses hardware task switching so it never depends on the
     * possibly corrupted stack that caused the double fault. */
    memset(&double_fault_tss, 0, sizeof(double_fault_tss));
    uint32_t emergency_top =
        (uint32_t)(uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    double_fault_tss.cr3 = (uint32_t)(uintptr_t)paging_kernel_directory();
    double_fault_tss.eip = (uint32_t)(uintptr_t)double_fault_emergency_entry;
    double_fault_tss.eflags = 0x00000002U;
    double_fault_tss.esp = emergency_top;
    double_fault_tss.ebp = emergency_top;
    double_fault_tss.esp0 = emergency_top;
    double_fault_tss.ss0 = kernel_ss;
    double_fault_tss.cs = 0x08U;
    double_fault_tss.ss = kernel_ss;
    double_fault_tss.ds = kernel_ss;
    double_fault_tss.es = kernel_ss;
    double_fault_tss.fs = kernel_ss;
    double_fault_tss.gs = kernel_ss;
    double_fault_tss.iomap_base = sizeof(tss_entry_t);
    
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

uint32_t tss_get_double_fault_base(void) {
    return (uint32_t)(uintptr_t)&double_fault_tss;
}

uint32_t tss_get_double_fault_limit(void) {
    return sizeof(double_fault_tss) - 1U;
}
