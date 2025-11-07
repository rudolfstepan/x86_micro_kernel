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
void ne2000_get_mac_address(uint8_t *mac);
void ne2000_send_packet(uint8_t *data, uint16_t length);
int ne2000_receive_packet(uint8_t *buffer, uint16_t buffer_size);

#endif // NE2000_H
