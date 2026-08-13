#ifndef PIT_H
#define PIT_H

#include <stdint.h>

extern void timer_irq_handler(void* r);
void timer_install(uint8_t ms);
void pit_delay(uint32_t milliseconds);
uint32_t pit_ticks(void);
uint64_t pit_monotonic_ms(void);

#endif // PIT_H
