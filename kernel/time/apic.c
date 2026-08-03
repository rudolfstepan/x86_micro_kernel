#include "kernel/time/apic.h"
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"
#include "kernel/sched/scheduler.h"

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

volatile uint32_t* apic = (volatile uint32_t*)APIC_BASE_ADDR;
volatile uint32_t apic_interrupt_count = 0;
extern void apic_timer_interrupt(void);

static bool cpu_has_local_apic(void) {
    uint32_t eax = 1, ebx, ecx, edx;
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

void enable_apic() {
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    apic_base |= APIC_BASE_ENABLE; // Enable the APIC
    write_msr(IA32_APIC_BASE_MSR, apic_base);

    apic = (volatile uint32_t*)(uintptr_t)(apic_base & 0xFFFFF000ULL);
    apic[0xF0 / 4] = APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR;
}

void init_apic_timer(uint32_t ticks) {
    // Set the divide configuration (divide by 16)
    apic[0x3E0 / 4] = 0x3;

    // Set the timer mode to periodic and assign the interrupt vector
    apic[0x320 / 4] = TIMER_PERIODIC_MODE | APIC_VECTOR_BASE;

    // Writing the initial count starts the timer, so do it last.
    apic[0x380 / 4] = ticks;
}

void initialize_apic_timer(void) {
    if (!cpu_has_local_apic()) {
        printf("Local APIC is not supported; scheduler timer remains disabled.\n");
        return;
    }

    /* Keep the LAPIC vector outside the remapped PIC range (0x20-0x2f). */
    uint32_t flags = irq_save();
    set_idt_entry(APIC_VECTOR_BASE, (uint32_t)(uintptr_t)apic_timer_interrupt);
    enable_apic();
    init_apic_timer(APIC_DEFAULT_TIMER_TICKS);
    irq_restore(flags);

    printf("APIC Timer configured in periodic mode on vector %u\n", APIC_VECTOR_BASE);
}
