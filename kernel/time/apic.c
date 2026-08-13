#include "kernel/time/apic.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/mm/paging.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

volatile uint32_t* apic = (volatile uint32_t*)APIC_BASE_ADDR;
volatile uint32_t apic_interrupt_count = 0;
extern void apic_timer_interrupt(void);

static bool cpu_has_local_apic(void) {
    uint32_t original_flags;
    uint32_t changed_flags;
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        : "=r"(original_flags)
        :: "memory"
    );
    uint32_t toggled_flags = original_flags ^ (1U << 21);
    __asm__ __volatile__(
        "push %0\n"
        "popf\n"
        :: "r"(toggled_flags)
        : "memory", "cc"
    );
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        : "=r"(changed_flags)
        :: "memory"
    );
    __asm__ __volatile__(
        "push %0\n"
        "popf\n"
        :: "r"(original_flags)
        : "memory", "cc"
    );
    if (((changed_flags ^ original_flags) & (1U << 21)) == 0) {
        return false;
    }

    uint32_t eax = 0, ebx, ecx, edx;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 1U) return false;
    eax = 1;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 9)) != 0;
}

void apic_timer_isr(void* r) {
    // Acknowledge before switching away from this interrupt stack. Otherwise
    // the LAPIC blocks subsequent timer vectors until this context returns.
    apic[0xB0 / 4] = 0;
    scheduler_interrupt_handler();
}

void apic_timer_stop() {
    // Mask the timer to stop it
    apic[APIC_LVT_TIMER / 4] |= TIMER_MASKED;
}

uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static bool enable_apic(void) {
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    uint64_t physical_base = apic_base & 0xFFFFFFFFFFFFF000ULL;
    if (physical_base > UINT32_MAX) return false;
    uint32_t physical_base32 = (uint32_t)physical_base;

    /* Process page directories are created by copying the kernel mappings.
     * The current ABI executes device IRQs while a user CR3 is active, so a
     * non-standard LAPIC base cannot be installed after user address spaces
     * already exist without propagating that PDE to each of them. */
    if (physical_base32 != APIC_BASE_ADDR) return false;

    void *mapping = map_kernel_mmio(physical_base32, 4096U);
    if (mapping == NULL) return false;
    apic = (volatile uint32_t*)mapping;
    apic_base |= APIC_BASE_ENABLE; // Enable the APIC
    write_msr(IA32_APIC_BASE_MSR, apic_base);
    apic[0xF0 / 4] = APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR;
    return true;
}

void init_apic_timer(uint32_t ticks) {
    // Set the divide configuration (divide by 16)
    apic[0x3E0 / 4] = 0x3;

    // Set the timer mode to periodic and assign the interrupt vector
    apic[0x320 / 4] = TIMER_PERIODIC_MODE | APIC_VECTOR_BASE;

    // Writing the initial count starts the timer, so do it last.
    apic[0x380 / 4] = ticks;
}

static uint32_t calibrate_apic_timer(void) {
    const uint64_t sample_ms = 20U;
    const uint32_t loop_limit = 100000000U;

    /* Run the local timer masked in one-shot mode while the already active
     * 1-ms PIT supplies the reference interval. */
    apic[APIC_TIMER_DIVIDE / 4] = 0x3;
    apic[APIC_LVT_TIMER / 4] = TIMER_MASKED | APIC_VECTOR_BASE;
    uint64_t start_ms = pit_monotonic_ms();
    apic[APIC_TIMER_INIT_CNT / 4] = UINT32_MAX;

    uint64_t elapsed_ms = 0;
    for (uint32_t loop = 0; loop < loop_limit; ++loop) {
        elapsed_ms = pit_monotonic_ms() - start_ms;
        if (elapsed_ms >= sample_ms) break;
        __asm__ __volatile__("pause");
    }
    uint32_t elapsed_counts = UINT32_MAX -
                              apic[APIC_TIMER_CURR_CNT / 4];
    if (elapsed_ms == 0 || elapsed_counts == 0) {
        return APIC_DEFAULT_TIMER_TICKS;
    }

    uint64_t periodic_counts =
        ((uint64_t)elapsed_counts * APIC_SCHEDULER_PERIOD_MS) / elapsed_ms;
    if (periodic_counts == 0 || periodic_counts > UINT32_MAX) {
        return APIC_DEFAULT_TIMER_TICKS;
    }
    return (uint32_t)periodic_counts;
}

void initialize_apic_timer(void) {
    scheduler_set_apic_timer_active(false);
    if (!cpu_has_local_apic()) {
        printf("Local APIC is not supported; using PIT scheduler fallback.\n");
        return;
    }

    /* Keep the LAPIC vector outside the remapped PIC range (0x20-0x2f). */
    uint32_t flags = irq_save();
    set_idt_entry(APIC_VECTOR_BASE, (uint32_t)(uintptr_t)apic_timer_interrupt);
    if (!enable_apic()) {
        irq_restore(flags);
        printf("Unable to map local APIC; using PIT scheduler fallback.\n");
        return;
    }
    irq_restore(flags);

    uint32_t ticks = calibrate_apic_timer();
    flags = irq_save();
    init_apic_timer(ticks);
    scheduler_set_apic_timer_active(true);
    irq_restore(flags);

    printf("APIC Timer calibrated to %u ms (%u ticks) on vector %u\n",
           APIC_SCHEDULER_PERIOD_MS, ticks, APIC_VECTOR_BASE);
}
