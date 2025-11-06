#ifndef NE2000_H
#define NE2000_H

#include <stdint.h>
#include <stdbool.h>

// NE2000 driver functions
void ne2000_detect();
void ne2000_test_send();
void ne2000_print_status();
bool ne2000_is_initialized();
void ne2000_print_mac_address();

#endif // NE2000_H
