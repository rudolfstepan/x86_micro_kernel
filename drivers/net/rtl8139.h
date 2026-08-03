#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include <stdbool.h>


bool rtl8139_send_packet(void* data, uint16_t len);
void rtl8139_receive_packet(void);
void rtl8139_poll_rx(void);
void rtl8139_get_mac_address(uint8_t *mac);
void rtl8139_detect(void);
void rtl8139_send_test_packet(void);
int rtl8139_is_initialized(void);

#endif  // RTL8139_H
