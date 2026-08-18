/** @file test/test_net_socket_host.c @brief Host behavior tests for UDP sockets. */
#include "drivers/net/net_socket.h"
#include "kernel/sched/wait_queue.h"
#include "kernel/sched/scheduler.h"

#include <stdint.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

static uint32_t sent_ip;
static uint16_t sent_source_port;
static uint16_t sent_destination_port;
static uint16_t sent_length;
static uint64_t monotonic_now;
static int send_result;
static uint32_t sleep_calls;

uint64_t pit_monotonic_ms(void) { return monotonic_now; }
int scheduler_sleep_ms(uint32_t milliseconds) {
    monotonic_now += milliseconds; ++sleep_calls; return 0;
}
int scheduler_yield(void) { return 0; }
void wait_queue_init(wait_queue_t *queue) { memset(queue, 0, sizeof(*queue)); }
int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms) {
    (void)queue; (void)kind; (void)deadline_ms; return -110;
}
bool wait_queue_wake_one_locked(wait_queue_t *queue) {
    (void)queue; return true;
}
size_t wait_queue_wake_all_locked(wait_queue_t *queue) {
    (void)queue; return 0U;
}

int udp_send(uint32_t ip, uint16_t source_port, uint16_t destination_port,
             const uint8_t *data, uint16_t length) {
    (void)data;
    sent_ip = ip; sent_source_port = source_port;
    sent_destination_port = destination_port; sent_length = length;
    return send_result;
}

int main(void) {
    net_socket_init();
    net_socket_handle_t socket = 0U, second = 0U;
    CHECK(net_socket_open(10, 2U, &socket) == 0 && socket != 0U);
    CHECK(net_socket_bind(10, 2U, socket, 9000U) == 0);
    CHECK(net_socket_open(11, 3U, &second) == 0);
    CHECK(net_socket_bind(11, 3U, second, 9000U) == -98);
    CHECK(net_socket_bind(11, 3U, second, 9001U) == 0);
    CHECK(net_socket_close(11, 99U, second) == -9);

    const uint8_t payload[] = {1U, 2U, 3U};
    CHECK(net_socket_sendto(10, 2U, socket, 0x0a000002U, 53U,
                            payload, sizeof(payload), 0U) == 3);
    CHECK(sent_ip == 0x0a000002U && sent_source_port == 9000U &&
          sent_destination_port == 53U && sent_length == 3U);
    send_result = -1; monotonic_now = 100U; sleep_calls = 0U;
    CHECK(net_socket_sendto(10, 2U, socket, 0x0a000002U, 53U,
                            payload, sizeof(payload), 40U) == -110);
    CHECK(sleep_calls == 2U && monotonic_now == 140U);
    send_result = 0;

    for (uint32_t index = 0U; index < NET_SOCKET_QUEUE_DEPTH; ++index)
        CHECK(net_socket_ingress(0x0a000002U, 53U, 9000U,
                                 payload, sizeof(payload)) == 0);
    CHECK(net_socket_ingress(0x0a000002U, 53U, 9000U,
                             payload, sizeof(payload)) == -105);
    net_socket_datagram_t datagram;
    uint8_t received[NET_SOCKET_MAX_DATAGRAM];
    CHECK(net_socket_recvfrom(10, 2U, socket, &datagram, received, 2U, 0U) == -90);
    CHECK(net_socket_recvfrom(10, 2U, socket, &datagram, received,
                              sizeof(received), 0U) == 3);
    CHECK(datagram.source_ip == 0x0a000002U && datagram.source_port == 53U &&
          datagram.destination_port == 9000U &&
          memcmp(received, payload, sizeof(payload)) == 0);

    net_socket_stats_t stats;
    net_socket_get_stats(&stats);
    CHECK(stats.active_sockets == 2U && stats.queued_datagrams == 3U &&
          stats.dropped_datagrams == 1U);
    net_socket_process_cleanup(10, 2U);
    CHECK(net_socket_recvfrom(10, 2U, socket, &datagram, received,
                              sizeof(received), 0U) == -9);
    net_socket_handle_t replacement = 0U;
    CHECK(net_socket_open(10, 4U, &replacement) == 0 && replacement != socket);
    return 0;
}
