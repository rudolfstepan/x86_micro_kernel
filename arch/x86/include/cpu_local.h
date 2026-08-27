/**
 * @file arch/x86/include/cpu_local.h
 * @brief Fixed-capacity per-CPU state for the i386 SMP substrate.
 */
#ifndef ARCH_X86_CPU_LOCAL_H
#define ARCH_X86_CPU_LOCAL_H

#include <stdbool.h>
#include <stdint.h>

#define X86_CPU_LOCAL_VERSION 6U
#define X86_CPU_LOCAL_MAX 16U
#define X86_CPU_INDEX_INVALID UINT32_MAX
#define X86_SCHEDULER_CONTEXT_WORDS 6U

struct page_directory;

typedef struct {
    uint32_t version;
    uint32_t cpu_index;
    uint32_t apic_id;
    volatile uint32_t registered;
    volatile uint32_t online;
    volatile uint32_t irq_context_depth;
    volatile uint32_t irq_context_vector;
    volatile uint32_t preempt_disable_count;
    volatile uint32_t preemption_pending;
    volatile int32_t scheduler_current_task;
    volatile uint32_t scheduler_context_saved;
    volatile int32_t scheduler_handoff_task;
    volatile uint32_t scheduler_handoff_action;
    volatile uint32_t tlb_observed_generation;
    volatile uint32_t lapic_timer_ticks;
    volatile uint32_t lapic_timer_calibrated;
    uint32_t kernel_idle_stack_low;
    uint32_t kernel_idle_stack_high;
    uint32_t scheduler_context[X86_SCHEDULER_CONTEXT_WORDS];
    struct page_directory *current_page_directory;
} x86_cpu_local_t;

/** Read the initial 8-bit APIC ID reported by CPUID leaf 1. */
uint8_t x86_cpu_initial_apic_id(void);

/** Initialize immutable BSP identity before paging and interrupt setup. */
bool x86_cpu_local_bootstrap(uint8_t bsp_apic_id);

/** Register one AP identity before issuing INIT/SIPI. */
bool x86_cpu_local_register(uint32_t cpu_index, uint8_t apic_id);

/** Validate current hardware identity and publish the local slot online. */
bool x86_cpu_local_mark_online(uint32_t cpu_index);

/** Resolve the calling processor; unknown identities fail closed to NULL. */
x86_cpu_local_t *x86_cpu_local_current(void);
x86_cpu_local_t *x86_cpu_local_by_index(uint32_t cpu_index);
uint32_t x86_cpu_current_index(void);

#endif
