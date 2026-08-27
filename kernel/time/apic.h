/**
 * @file kernel/time/apic.h
 * @brief APIC-Timer-Schnittstelle.
 *
 * Layer: Ring-0 x86 time backend.
 * Contract: Erfolgreiche Initialisierung liefert monotone, begrenzte Timerdienste.
 * Safety: Ungültige Hardwarewerte aktivieren das Backend nicht; PIT bleibt Basis-Fallback.
 */
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
#define APIC_IPI_INIT_ASSERT 0x0000C500U
#define APIC_IPI_INIT_DEASSERT 0x00008500U
#define APIC_IPI_STARTUP 0x00000600U
#define APIC_IPI_FIXED(vector) ((uint32_t)(vector) & 0xFFU)
#define APIC_SPURIOUS_VECTOR 0xFF
#define APIC_SPURIOUS_ENABLE (1U << 8)
#define APIC_DEFAULT_TIMER_TICKS 1000000U
#define APIC_SCHEDULER_PERIOD_MS 10U
#define IA32_APIC_BASE_MSR  0x1B
#define APIC_BASE_ENABLE    (1 << 11)
#define TIMER_PERIODIC_MODE (1 << 17)
#define TIMER_MASKED        (1 << 16) // Used to disable the timer

#include <stdbool.h>

extern volatile uint32_t* apic;

void init_apic_timer(uint32_t ticks);
void initialize_apic_timer(void);
bool apic_is_available(void);
bool apic_enable_current_cpu_ipi_only(void);
bool apic_calibrate_current_cpu_timer_masked(uint32_t *ticks_out);
bool apic_start_current_cpu_scheduler_timer(void);
uint8_t apic_local_id(void);
bool apic_send_ipi(uint8_t destination_apic_id, uint32_t command,
                   uint64_t deadline_ms);
bool apic_send_ipi_bounded(uint8_t destination_apic_id, uint32_t command,
                           uint32_t spin_limit);
void apic_eoi(void);
//void apic_timer_set_periodic(uint32_t interval);


#endif // APIC_H
