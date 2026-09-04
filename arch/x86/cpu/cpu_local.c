/**
 * @file arch/x86/cpu/cpu_local.c
 * @brief Bounded xAPIC-ID to per-CPU-state mapping.
 */
#include "arch/x86/include/cpu_local.h"

#ifdef REIST_HOST_TEST
#include <string.h>
extern uint8_t x86_cpu_test_apic_id(void);
extern void x86_cpu_test_gdtr(uint32_t *base, uint16_t *limit);
#else
#include "lib/libc/string.h"
#endif

#include <stddef.h>
#include <stdint.h>

#define APIC_ID_COUNT 256U
#define CPU_INDEX_UNMAPPED 0xFFU

static x86_cpu_local_t cpu_locals[X86_CPU_LOCAL_MAX];
static uint8_t apic_to_cpu[APIC_ID_COUNT];
static bool cpu_local_initialized;
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) cpu_gdtr_t;
static cpu_gdtr_t cpu_gdtrs[X86_CPU_LOCAL_MAX];
static uint32_t cpu_gdtr_bound[X86_CPU_LOCAL_MAX];

static cpu_gdtr_t current_gdtr(void) {
    cpu_gdtr_t descriptor;
#ifdef REIST_HOST_TEST
    uint32_t base;
    uint16_t limit;
    x86_cpu_test_gdtr(&base, &limit);
    descriptor.base = base;
    descriptor.limit = limit;
#else
    /* Intel SDM SGDT, 32-bit operand size: six bytes, independent of the
     * current task's user segment selectors. Only Ring 0 can change GDTR. */
    __asm__ __volatile__("sgdt %0" : "=m"(descriptor) : : "memory");
#endif
    return descriptor;
}

uint8_t x86_cpu_initial_apic_id(void) {
#ifdef REIST_HOST_TEST
    return x86_cpu_test_apic_id();
#else
    uint32_t original_flags;
    uint32_t changed_flags;
    __asm__ __volatile__("pushf\n pop %0" : "=r"(original_flags));
    uint32_t toggled_flags = original_flags ^ (1U << 21U);
    __asm__ __volatile__("push %0\n popf" : : "r"(toggled_flags)
                         : "memory", "cc");
    __asm__ __volatile__("pushf\n pop %0" : "=r"(changed_flags));
    __asm__ __volatile__("push %0\n popf" : : "r"(original_flags)
                         : "memory", "cc");
    if (((changed_flags ^ original_flags) & (1U << 21U)) == 0U) return 0U;

    uint32_t maximum_leaf = 0U;
    uint32_t unused_b;
    uint32_t unused_c;
    uint32_t unused_d;
    __asm__ __volatile__("cpuid"
                         : "+a"(maximum_leaf), "=b"(unused_b),
                           "=c"(unused_c), "=d"(unused_d));
    if (maximum_leaf < 1U) return 0U;
    uint32_t eax = 1U;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (uint8_t)(ebx >> 24U);
#endif
}

bool x86_cpu_local_bind_gdt(uint32_t cpu_index) {
    if (!cpu_local_initialized || cpu_index >= X86_CPU_LOCAL_MAX ||
        cpu_locals[cpu_index].registered == 0U ||
        __atomic_load_n(&cpu_gdtr_bound[cpu_index], __ATOMIC_ACQUIRE) != 0U ||
        cpu_locals[cpu_index].apic_id != x86_cpu_initial_apic_id())
        return false;
    cpu_gdtr_t descriptor = current_gdtr();
    if (descriptor.base == 0U || descriptor.limit != 7U * 8U - 1U)
        return false;
    /* BSP starts APs serially. Bind only after LGDT and before online/IRQs;
     * published records are immutable and may be read by every CPU. */
    for (uint32_t index = 0U; index < X86_CPU_LOCAL_MAX; ++index) {
        if (__atomic_load_n(&cpu_gdtr_bound[index], __ATOMIC_ACQUIRE) != 0U &&
            cpu_gdtrs[index].base == descriptor.base) return false;
    }
    cpu_gdtrs[cpu_index] = descriptor;
    __atomic_store_n(&cpu_gdtr_bound[cpu_index], 1U, __ATOMIC_RELEASE);
    return true;
}

static void initialize_slot(uint32_t cpu_index, uint8_t apic_id) {
    x86_cpu_local_t *local = &cpu_locals[cpu_index];
    memset(local, 0, sizeof(*local));
    local->version = X86_CPU_LOCAL_VERSION;
    local->cpu_index = cpu_index;
    local->apic_id = apic_id;
    local->scheduler_current_task = -1;
    local->scheduler_handoff_task = -1;
    __sync_synchronize();
    local->registered = 1U;
    apic_to_cpu[apic_id] = (uint8_t)cpu_index;
    __sync_synchronize();
}

bool x86_cpu_local_bootstrap(uint8_t bsp_apic_id) {
    if (cpu_local_initialized) return false;
    memset(cpu_locals, 0, sizeof(cpu_locals));
    memset(apic_to_cpu, CPU_INDEX_UNMAPPED, sizeof(apic_to_cpu));
    initialize_slot(0U, bsp_apic_id);
    cpu_locals[0].online = 1U;
    __sync_synchronize();
    cpu_local_initialized = true;
    return true;
}

bool x86_cpu_local_register(uint32_t cpu_index, uint8_t apic_id) {
    if (!cpu_local_initialized || cpu_index == 0U ||
        cpu_index >= X86_CPU_LOCAL_MAX ||
        apic_to_cpu[apic_id] != CPU_INDEX_UNMAPPED ||
        cpu_locals[cpu_index].registered != 0U) return false;
    initialize_slot(cpu_index, apic_id);
    return true;
}

x86_cpu_local_t *x86_cpu_local_by_index(uint32_t cpu_index) {
    if (!cpu_local_initialized || cpu_index >= X86_CPU_LOCAL_MAX ||
        cpu_locals[cpu_index].registered == 0U) return NULL;
    return &cpu_locals[cpu_index];
}

x86_cpu_local_t *x86_cpu_local_current(void) {
    if (!cpu_local_initialized) return NULL;
    cpu_gdtr_t descriptor = current_gdtr();
    for (uint32_t index = 0U; index < X86_CPU_LOCAL_MAX; ++index) {
        if (__atomic_load_n(&cpu_gdtr_bound[index], __ATOMIC_ACQUIRE) != 0U &&
            cpu_gdtrs[index].base == descriptor.base &&
            cpu_gdtrs[index].limit == descriptor.limit)
            return &cpu_locals[index];
    }
    /* Bootstrap uses a shared loader/trampoline GDT. Once this CPU has bound
     * its private table, an unexpected GDTR must fail closed, not disguise
     * the mismatch by falling back to CPUID. */
    uint8_t apic_id = x86_cpu_initial_apic_id();
    uint8_t cpu_index = apic_to_cpu[apic_id];
    if (cpu_index == CPU_INDEX_UNMAPPED || cpu_index >= X86_CPU_LOCAL_MAX)
        return NULL;
    x86_cpu_local_t *local = &cpu_locals[cpu_index];
    if (local->registered == 0U || local->apic_id != apic_id ||
        __atomic_load_n(&cpu_gdtr_bound[cpu_index], __ATOMIC_ACQUIRE) != 0U)
        return NULL;
    return local;
}

uint32_t x86_cpu_current_index(void) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    return local != NULL ? local->cpu_index : X86_CPU_INDEX_INVALID;
}

bool x86_cpu_local_mark_online(uint32_t cpu_index) {
    x86_cpu_local_t *local = x86_cpu_local_by_index(cpu_index);
    if (local == NULL || local != x86_cpu_local_current() ||
        local->online != 0U) return false;
    __sync_synchronize();
    local->online = 1U;
    __sync_synchronize();
    return true;
}
