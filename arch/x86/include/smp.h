/**
 * @file arch/x86/include/smp.h
 * @brief Bounded x86 application-processor discovery and bring-up.
 *
 * The first SMP stage activates each xAPIC processor, gives it a private
 * bootstrap stack, verifies execution, and parks it with interrupts disabled.
 * Scheduling remains BSP-only until scheduler, TSS, and IRQ state are per-CPU.
 */
#ifndef ARCH_X86_SMP_H
#define ARCH_X86_SMP_H

#include <stdbool.h>
#include <stdint.h>

#define X86_SMP_MAX_CPUS 16U
#define X86_SMP_TRAMPOLINE_BASE 0x00007000U
#define X86_SMP_TRAMPOLINE_REGION_SIZE 0x00001000U
#define X86_SMP_SCHEDULER_RELEASE_VECTOR 0xF2U
#define X86_SMP_RESCHEDULE_VECTOR 0xF3U

typedef struct {
    uint32_t version;
    uint32_t discovered_cpu_count;
    uint32_t usable_cpu_count;
    uint32_t online_cpu_count;
    uint32_t parked_ap_count;
    uint32_t failed_ap_count;
    uint32_t bsp_apic_id;
    uint32_t apic_ids[X86_SMP_MAX_CPUS];
} x86_smp_status_t;

typedef bool (*x86_smp_parallel_probe_t)(uint32_t cpu_index);

/** Discover, start, verify, and park all bounded xAPIC application CPUs. */
bool x86_smp_initialize(void);

/** Register one bounded read-only subsystem probe before AP release. */
bool x86_smp_set_parallel_probe(x86_smp_parallel_probe_t probe);

/**
 * Run one isolated kernel task on every online AP and release its local
 * scheduler timer only after the BSP has completed service publication.
 */
bool x86_smp_scheduler_probe(void);

/** Dedicated AP scheduler-release IPI entry called by the assembly stub. */
void x86_smp_scheduler_release_isr(void *frame);

/**
 * Request an immediate scheduler pass on eligible remote CPUs in cpu_mask.
 * Delivery is bounded and advisory; READY state remains authoritative when
 * the periodic scheduler must provide the fallback.
 */
bool x86_smp_request_reschedule(uint32_t cpu_mask);

/** Dedicated cross-CPU reschedule IPI entry called by the assembly stub. */
void x86_smp_reschedule_isr(void *frame);

/** Return a coherent BSP-owned status snapshot. */
void x86_smp_status(x86_smp_status_t *status);

/** Entry called by the low-memory trampoline; it never returns. */
__attribute__((noreturn)) void x86_smp_ap_entry(uint32_t cpu_index);

#endif
