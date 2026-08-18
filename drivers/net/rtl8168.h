/**
 * @file drivers/net/rtl8168.h
 * @brief RTL8111/RTL8168-NIC-Vertrag.
 *
 * Layer: Ring-0 network driver and mediation.
 * Contract: Frames und Gerätezustand werden vor Veröffentlichung vollständig validiert.
 * Safety: DMA-Ringe und Descriptorstatus bilden die Backendgrenze.
 */
#ifndef RTL8168_H
#define RTL8168_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool rtl8168_send_packet(const uint8_t *packet, size_t length);
void rtl8168_poll_rx(void);
void rtl8168_get_mac_address(uint8_t mac[6]);
void rtl8168_detect(void);
int rtl8168_is_initialized(void);
bool rtl8168_is_link_up(void);
void rtl8168_fence_outputs(void);
bool rtl8168_outputs_fenced(void);

#endif  /* RTL8168_H */
