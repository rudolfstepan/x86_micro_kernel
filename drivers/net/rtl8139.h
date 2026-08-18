/**
 * @file drivers/net/rtl8139.h
 * @brief RTL8139-NIC- und Ringpuffervertrag.
 *
 * Layer: Ring-0 network driver and mediation.
 * Contract: Frames und Gerätezustand werden vor Veröffentlichung vollständig validiert.
 * Safety: CAPR/CBR und TX-Slots werden nur in gültiger Hardwarefolge verändert.
 */
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
void rtl8139_fence_outputs(void);
bool rtl8139_outputs_fenced(void);

#endif  // RTL8139_H
