#ifndef NETDEV_H
#define NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool netdev_available(void);
const char* netdev_backend_name(void);
bool netdev_send(const uint8_t* packet, size_t length);
int netdev_receive_frame(uint8_t* buffer, size_t capacity);
int netdev_receive_service_frame(uint8_t* buffer, size_t capacity);
bool netdev_get_mac_address(uint8_t mac[6]);
void netdev_deliver_rx(const uint8_t* packet, uint16_t length);
void netdev_poll(void);
void netdev_reset_monitor(void);
void netdev_reset_service_frames(void);
void netdev_fence_outputs(void);
bool netdev_outputs_fenced(void);
bool netdev_supervision_init(uint64_t now_ms);

#endif
