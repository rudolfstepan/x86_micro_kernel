#include "kernel/time/apic.h"
#include "arch/x86/include/sys.h"
#include "kernel/sched/scheduler.h"

#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"

#include <stdint.h>
#include <stddef.h>

volatile uint32_t* apic = (volatile uint32_t*)APIC_BASE_ADDR;
volatile uint32_t apic_interrupt_count = 0;

void apic_timer_isr(void* r) {
    // Acknowledge the APIC interrupt
    //apic[0xB0 / 4] = 0;  // End of interrupt (EOI)

    //printf("APIC Timer interrupt %u\n", apic_interrupt_count++);

    scheduler_interrupt_handler();

    // Acknowledge the interrupt (APIC End-of-Interrupt)
    //outb(0x22, 0x22); // Send EOI to the APIC

    apic[0xB0 / 4] = 0;  // End of interrupt (EOI)
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
}

void init_apic_timer(uint32_t ticks) {
    // Set the divide configuration (divide by 16)
    apic[0x3E0 / 4] = 0x3;

    // Set the initial timer count
    apic[0x380 / 4] = ticks;

    // Set the timer mode to periodic and assign the interrupt vector
    apic[0x320 / 4] = 0x20000 | APIC_VECTOR_BASE; //32;  // Periodic mode, interrupt vector 32
}

void initialize_apic_timer() {
    // Enable the APIC
    enable_apic();

    // Register the interrupt handler for the APIC Timer
    register_interrupt_handler(2, (void*)apic_timer_isr);

    printf("APIC Timer configured in periodic mode on vector %u\n", APIC_VECTOR_BASE);
}
