/**
 * @file drivers/net/tcp_socket.h
 * @brief Fixed-capacity active/passive TCP control blocks.
 */
#ifndef DRIVERS_NET_TCP_SOCKET_H
#define DRIVERS_NET_TCP_SOCKET_H

#include <stdint.h>

#define TCP_SOCKET_ABI_VERSION 1U
#define TCP_SOCKET_MAX_SOCKETS 4U
#define TCP_SOCKET_MAX_SEGMENT 512U
#define TCP_SOCKET_RECEIVE_CAPACITY 2048U
#define TCP_SOCKET_MAX_TIMEOUT_MS 30000U
#define TCP_SOCKET_MAX_BACKLOG 2U

typedef uint32_t tcp_socket_handle_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t source_ip;
    uint32_t destination_ip;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t window;
    uint16_t length;
    uint8_t flags;
    uint8_t reserved[3];
} tcp_socket_segment_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_sockets;
    uint32_t established_sockets;
    uint32_t received_bytes;
    uint32_t dropped_segments;
    uint32_t retransmissions;
} tcp_socket_stats_t;

void tcp_socket_init(void);
int tcp_socket_open(int pid, uint32_t process_generation,
                    tcp_socket_handle_t *handle_out);
int tcp_socket_connect(int pid, uint32_t process_generation,
                       tcp_socket_handle_t handle, uint32_t destination_ip,
                       uint16_t destination_port, uint32_t timeout_ms);
int tcp_socket_listen(int pid, uint32_t process_generation,
                      tcp_socket_handle_t handle, uint16_t local_port,
                      uint16_t backlog);
int tcp_socket_accept(int pid, uint32_t process_generation,
                      tcp_socket_handle_t listener,
                      tcp_socket_handle_t *accepted_out,
                      uint32_t *peer_ip_out, uint16_t *peer_port_out,
                      uint32_t timeout_ms);
int tcp_socket_send(int pid, uint32_t process_generation,
                    tcp_socket_handle_t handle, const uint8_t *data,
                    uint32_t length, uint32_t timeout_ms);
int tcp_socket_receive(int pid, uint32_t process_generation,
                       tcp_socket_handle_t handle, uint8_t *data,
                       uint32_t capacity, uint32_t timeout_ms);
int tcp_socket_close(int pid, uint32_t process_generation,
                     tcp_socket_handle_t handle, uint32_t timeout_ms);
void tcp_socket_process_cleanup(int pid, uint32_t process_generation);
int tcp_socket_ingress(const tcp_socket_segment_t *segment,
                       const uint8_t *data);
void tcp_socket_get_stats(tcp_socket_stats_t *stats_out);

#endif
