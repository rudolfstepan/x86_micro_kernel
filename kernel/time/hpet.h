#ifndef HPET_H
#define HPET_H

#include <stdint.h>


void hpet_init();
void test_hpet_main_counter();
void initialize_hpet_periodic_callback(uint64_t interval_ns);

#endif