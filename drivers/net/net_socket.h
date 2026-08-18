/**
 * @file drivers/net/net_socket.h
 * @brief Fixed-capacity process-owned IPv4/UDP socket objects.
 *
 * Socket handles are generation-scoped capabilities.  The subsystem never
 * allocates from the heap and every receive queue has a fixed upper bound.
 */
#ifndef DRIVERS_NET_NET_SOCKET_H
#define DRIVERS_NET_NET_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#define NET_SOCKET_ABI_VERSION 1U
#define NET_SOCKET_MAX_SOCKETS 8U
#define NET_SOCKET_QUEUE_DEPTH 4U
#define NET_SOCKET_MAX_DATAGRAM 512U

typedef uint32_t net_socket_handle_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_ip;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t length;
} net_socket_datagram_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_sockets;
    uint32_t queued_datagrams;
    uint32_t dropped_datagrams;
} net_socket_stats_t;

void net_socket_init(void);
int net_socket_open(int pid, uint32_t process_generation,
                    net_socket_handle_t *handle_out);
int net_socket_bind(int pid, uint32_t process_generation,
                    net_socket_handle_t handle, uint16_t port);
int net_socket_sendto(int pid, uint32_t process_generation,
                      net_socket_handle_t handle, uint32_t destination_ip,
                      uint16_t destination_port, const uint8_t *data,
                      uint32_t length, uint32_t timeout_ms);
int net_socket_recvfrom(int pid, uint32_t process_generation,
                        net_socket_handle_t handle,
                        net_socket_datagram_t *datagram_out, uint8_t *data,
                        uint32_t capacity, uint32_t timeout_ms);
int net_socket_close(int pid, uint32_t process_generation,
                     net_socket_handle_t handle);
void net_socket_process_cleanup(int pid, uint32_t process_generation);
int net_socket_ingress(uint32_t source_ip, uint16_t source_port,
                       uint16_t destination_port, const uint8_t *data,
                       uint32_t length);
void net_socket_get_stats(net_socket_stats_t *stats_out);

#endif
