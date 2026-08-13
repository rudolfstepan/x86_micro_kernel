#ifndef KERNEL_WATCHDOG_H
#define KERNEL_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

void watchdog_init(void);
void watchdog_health_progress(void);
void watchdog_clock_tick(uint64_t monotonic_ms);
bool watchdog_fatal_handoff(void);
bool watchdog_available(void);

#endif
