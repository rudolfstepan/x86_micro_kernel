#ifndef ARCH_X86_TSS_H
#define ARCH_X86_TSS_H

#include <stdint.h>

/**
 * @file tss.h
 * @brief Task State Segment (TSS) for x86 privilege level switching
 * 
 * The TSS is required for switching between Ring 0 (kernel) and Ring 3 (user).
 * When a user mode program triggers an interrupt or syscall, the CPU loads
 * ESP0 and SS0 from the TSS to switch to the kernel stack.
 */

/**
 * x86 Task State Segment structure
 * 
 * Only ESP0 and SS0 are used in modern 32-bit protected mode.
 * Other fields are legacy from hardware task switching (not used).
 */
typedef struct {
    uint32_t prev_tss;   // Previous TSS (not used)
    uint32_t esp0;       // Stack pointer for Ring 0 (kernel)
    uint32_t ss0;        // Stack segment for Ring 0 (kernel)
    uint32_t esp1;       // Stack pointer for Ring 1 (not used)
    uint32_t ss1;        // Stack segment for Ring 1 (not used)
    uint32_t esp2;       // Stack pointer for Ring 2 (not used)
    uint32_t ss2;        // Stack segment for Ring 2 (not used)
    uint32_t cr3;        // Page directory base (not used)
    uint32_t eip;        // Instruction pointer (not used)
    uint32_t eflags;     // Flags register (not used)
    uint32_t eax;        // General purpose registers (not used)
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;         // Segment selectors (not used)
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;        // LDT selector (not used)
    uint16_t trap;       // Debug trap flag (not used)
    uint16_t iomap_base; // I/O permission bitmap base (not used)
} __attribute__((packed)) tss_entry_t;

/**
 * Initialize the Task State Segment
 * 
 * Sets up TSS with kernel stack pointer (ESP0) and segment (SS0).
 * Must be called after GDT is set up with TSS descriptor.
 * 
 * @param kernel_stack Pointer to top of kernel stack
 * @param kernel_ss Kernel stack segment selector (usually 0x10)
 */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss);

/**
 * Update TSS kernel stack pointer
 * 
 * Call this when switching processes to ensure interrupts/syscalls
 * from user mode switch to the correct kernel stack.
 * 
 * @param kernel_stack New kernel stack pointer
 */
void tss_set_kernel_stack(uint32_t kernel_stack);

/**
 * Get TSS base address
 * 
 * Used by GDT setup to create TSS descriptor.
 * 
 * @return Physical address of TSS structure
 */
uint32_t tss_get_base(void);

/**
 * Get TSS size
 * 
 * Used by GDT setup to create TSS descriptor.
 * 
 * @return Size of TSS structure in bytes
 */
uint32_t tss_get_limit(void);

#endif // ARCH_X86_TSS_H
