#ifndef PIT_H
#define PIT_H

#include <stdint.h>

extern void timer_irq_handler(void* r);
void timer_install(uint8_t ms);
void pit_delay(uint32_t milliseconds);

#endif // PIT_H