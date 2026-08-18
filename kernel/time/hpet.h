/**
 * @file kernel/time/hpet.h
 * @brief Optionale HPET-Zeitquellenschnittstelle.
 *
 * Layer: Ring-0 x86 time backend.
 * Contract: Erfolgreiche Initialisierung liefert monotone, begrenzte Timerdienste.
 * Safety: Ungültige Hardwarewerte aktivieren das Backend nicht; PIT bleibt Basis-Fallback.
 */
#ifndef HPET_H
#define HPET_H

#include <stdint.h>


void hpet_init();
void test_hpet_main_counter();
void initialize_hpet_periodic_callback(uint64_t interval_ns);

#endif
