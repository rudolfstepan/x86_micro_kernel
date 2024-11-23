#include "apic.h"
#include "sys.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

#include <stdint.h>
#include <stddef.h>


volatile uint32_t* apic = (volatile uint32_t*)APIC_BASE_ADDR;
volatile uint32_t apic_interrupt_count = 0;

void apic_timer_isr(void* r) {
    apic_interrupt_count++;
    apic[0xB0 / 4] = 0; // End of interrupt (EOI)
    // Perform periodic tasks
    //printf("APIC Timer Interrupt Triggered!\n");

    // TODO: Implement a scheduler to switch tasks
}

void apic_timer_set_periodic(uint32_t interval) {
    // Step 1: Set the timer divisor (divide by 16 in this example)
    apic[APIC_TIMER_DIVIDE / 4] = 0x03; // Divide by 16

    // Step 2: Set the timer mode to periodic
    apic[APIC_LVT_TIMER / 4] = TIMER_PERIODIC_MODE | APIC_VECTOR_BASE;

    // Step 3: Set the initial count value
    apic[APIC_TIMER_INIT_CNT / 4] = interval;
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

void initialize_apic_timer() {
    // Enable the APIC
    enable_apic();

    // Register the interrupt handler for the APIC Timer
    irq_install_handler(2, apic_timer_isr);

    printf("APIC Timer configured in periodic mode on vector %u\n", APIC_VECTOR_BASE);
}
