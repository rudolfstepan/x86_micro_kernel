#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>


void initialize_rtl8139(uint8_t bus, uint8_t device, uint8_t function);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void rtl8139_send_packet(void* data, uint16_t len);
void rtl8139_receive_packet();
void test_loopback();
void rtl8139_init();
int find_rtl8139();
void rtl8139_detect();
void rtl8139_send_test_packet();
int rtl8139_is_initialized();

#endif  // RTL8139_H