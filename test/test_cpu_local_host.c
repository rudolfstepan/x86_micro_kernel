#include "arch/x86/include/cpu_local.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static uint8_t hardware_id;
static uint32_t descriptor_base;
static uint16_t descriptor_limit = 55U;
static unsigned cpuid_reads;

uint8_t x86_cpu_test_apic_id(void) {
    ++cpuid_reads;
    return hardware_id;
}
void x86_cpu_test_gdtr(uint32_t *base, uint16_t *limit) {
    *base = descriptor_base;
    *limit = descriptor_limit;
}

int main(void) {
    assert(x86_cpu_local_current() == NULL);
    assert(x86_cpu_local_bootstrap(0U));
    assert(!x86_cpu_local_bootstrap(0U));
    assert(x86_cpu_current_index() == 0U);
    assert(!x86_cpu_local_bind_gdt(0U)); /* absent table */
    descriptor_base = 0x1000U;
    descriptor_limit = 54U;
    assert(!x86_cpu_local_bind_gdt(0U));
    descriptor_limit = 55U;
    assert(x86_cpu_local_bind_gdt(0U));
    assert(!x86_cpu_local_bind_gdt(0U)); /* immutable */
    for (uint32_t cpu = 1; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        hardware_id = (uint8_t)(cpu * 7U);
        descriptor_base = 0x8000U; /* shared AP bootstrap table */
        assert(x86_cpu_local_register(cpu, hardware_id));
        assert(x86_cpu_current_index() == cpu);
        assert(!x86_cpu_local_bind_gdt(0U));
        descriptor_base = 0x1000U;
        assert(!x86_cpu_local_bind_gdt(cpu)); /* duplicate table */
        descriptor_base = 0x1000U + cpu * 0x100U;
        assert(x86_cpu_local_bind_gdt(cpu));
        assert(x86_cpu_local_mark_online(cpu));
    }
    unsigned before = cpuid_reads;
    for (uint32_t cpu = 0; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        descriptor_base = 0x1000U + cpu * 0x100U;
        for (unsigned repeat = 0; repeat < 1000; ++repeat) {
            assert(x86_cpu_current_index() == cpu);
            assert(x86_cpu_local_current() == x86_cpu_local_by_index(cpu));
        }
    }
    assert(cpuid_reads == before);
    descriptor_limit = 54U;
    assert(x86_cpu_local_current() == NULL);
    descriptor_limit = 55U;
    descriptor_base = 0xBAD000U;
    assert(x86_cpu_local_current() == NULL); /* bound CPU lost its table */
    hardware_id = 254U;
    assert(x86_cpu_local_current() == NULL);
    assert(!x86_cpu_local_bind_gdt(X86_CPU_LOCAL_MAX));
    puts("CPU_LOCAL_GDTR_HOST_OK slots=16 runtime_cpuid=0");
    return 0;
}
