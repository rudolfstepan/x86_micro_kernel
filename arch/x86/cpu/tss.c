/**
 * @file tss.c
 * @brief Task State Segment implementation for privilege switching
 * 
 * The TSS provides the CPU with kernel stack information when transitioning
 * from Ring 3 (user mode) to Ring 0 (kernel mode) during interrupts/syscalls.
 */

#include "arch/x86/include/tss.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/mm/paging.h"
#include "include/kernel/fatal.h"
#include "lib/libc/string.h"
#include <stddef.h>

// Global TSS instance
static tss_entry_t kernel_tss;
static tss_entry_t double_fault_tss;
static uint8_t double_fault_stack[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static tss_entry_t ap_kernel_tss[X86_CPU_LOCAL_MAX - 1U];
static tss_entry_t ap_double_fault_tss[X86_CPU_LOCAL_MAX - 1U];
static uint8_t ap_double_fault_stacks[X86_CPU_LOCAL_MAX - 1U][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));

static tss_entry_t *runtime_tss_for_cpu(uint32_t cpu_index) {
    if (cpu_index >= X86_CPU_LOCAL_MAX) return NULL;
    return cpu_index == 0U ? &kernel_tss : &ap_kernel_tss[cpu_index - 1U];
}

static tss_entry_t *fault_tss_for_cpu(uint32_t cpu_index) {
    if (cpu_index >= X86_CPU_LOCAL_MAX) return NULL;
    return cpu_index == 0U ? &double_fault_tss
                           : &ap_double_fault_tss[cpu_index - 1U];
}

static uint8_t *fault_stack_for_cpu(uint32_t cpu_index) {
    if (cpu_index >= X86_CPU_LOCAL_MAX) return NULL;
    return cpu_index == 0U ? double_fault_stack
                           : ap_double_fault_stacks[cpu_index - 1U];
}

/**
 * Initialize the Task State Segment
 */
void tss_init(uint32_t kernel_stack, uint32_t kernel_ss) {
    (void)tss_init_cpu(0U, kernel_stack, kernel_ss);
}

bool tss_init_cpu(uint32_t cpu_index, uint32_t kernel_stack,
                  uint32_t kernel_ss) {
    tss_entry_t *runtime = runtime_tss_for_cpu(cpu_index);
    tss_entry_t *fault = fault_tss_for_cpu(cpu_index);
    uint8_t *fault_stack = fault_stack_for_cpu(cpu_index);
    if (runtime == NULL || fault == NULL || fault_stack == NULL ||
        kernel_stack == 0U || kernel_ss == 0U) return false;
    memset(runtime, 0, sizeof(*runtime));
    runtime->ss0 = kernel_ss;
    runtime->esp0 = kernel_stack;
    runtime->iomap_base = sizeof(*runtime);

    /* Vector 8 uses hardware task switching so it never depends on the
     * possibly corrupted stack that caused the double fault. */
    memset(fault, 0, sizeof(*fault));
    uint32_t emergency_top =
        (uint32_t)(uintptr_t)(fault_stack + PAGE_SIZE);
    fault->cr3 = (uint32_t)(uintptr_t)paging_kernel_directory();
    fault->eip = (uint32_t)(uintptr_t)double_fault_emergency_entry;
    fault->eflags = 0x00000002U;
    fault->esp = emergency_top;
    fault->ebp = emergency_top;
    fault->esp0 = emergency_top;
    fault->ss0 = kernel_ss;
    fault->cs = 0x08U;
    fault->ss = kernel_ss;
    fault->ds = kernel_ss;
    fault->es = kernel_ss;
    fault->fs = kernel_ss;
    fault->gs = kernel_ss;
    fault->iomap_base = sizeof(*fault);
    
    // CS and SS are set by the CPU during task switch
    // We don't use hardware task switching, so these remain 0
    return true;
}

/**
 * Update kernel stack pointer in TSS
 * 
 * This should be called when switching between processes to ensure
 * each process uses its own kernel stack when entering kernel mode.
 */
void tss_set_kernel_stack(uint32_t kernel_stack) {
    tss_entry_t *runtime = runtime_tss_for_cpu(x86_cpu_current_index());
    if (runtime != NULL) runtime->esp0 = kernel_stack;
}

/**
 * Get TSS base address for GDT descriptor
 */
uint32_t tss_get_base(void) {
    return (uint32_t)&kernel_tss;
}

uint32_t tss_get_base_for_cpu(uint32_t cpu_index) {
    tss_entry_t *runtime = runtime_tss_for_cpu(cpu_index);
    return (uint32_t)(uintptr_t)runtime;
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

uint32_t tss_get_double_fault_base_for_cpu(uint32_t cpu_index) {
    tss_entry_t *fault = fault_tss_for_cpu(cpu_index);
    return (uint32_t)(uintptr_t)fault;
}

uint32_t tss_get_double_fault_limit(void) {
    return sizeof(double_fault_tss) - 1U;
}
