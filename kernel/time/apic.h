#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stddef.h>

#define APIC_VECTOR_BASE    0xF0
#define APIC_BASE_ADDR      0xFEE00000
#define APIC_LVT_TIMER      0x320
#define APIC_TIMER_DIVIDE   0x3E0
#define APIC_TIMER_INIT_CNT 0x380
#define APIC_TIMER_CURR_CNT 0x390
#define APIC_SPURIOUS_VECTOR 0xFF
#define APIC_SPURIOUS_ENABLE (1U << 8)
#define APIC_DEFAULT_TIMER_TICKS 1000000U
#define IA32_APIC_BASE_MSR  0x1B
#define APIC_BASE_ENABLE    (1 << 11)
#define TIMER_PERIODIC_MODE (1 << 17)
#define TIMER_MASKED        (1 << 16) // Used to disable the timer

extern volatile uint32_t* apic;

void init_apic_timer(uint32_t ticks);
void initialize_apic_timer(void);
//void apic_timer_set_periodic(uint32_t interval);


#endif // APIC_H
