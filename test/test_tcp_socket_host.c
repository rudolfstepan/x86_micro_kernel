/** @file test/test_tcp_socket_host.c @brief TCP state-machine host test. */
#include "drivers/net/tcp_socket.h"
#include "drivers/net/netstack.h"
#include "kernel/sched/scheduler.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

static uint64_t now_ms = 1U;
static uint32_t sent_ip, sent_sequence, sent_acknowledgement;
static uint16_t sent_source_port, sent_destination_port, sent_length;
static uint8_t sent_flags;
static bool pending_reply;
static int reply_result;

uint64_t pit_monotonic_ms(void) { return now_ms; }
uint32_t netstack_get_ip_address(void) { return 0x0a000001U; }
bool netstack_send_tcp_segment(uint32_t ip, uint16_t source_port,
                               uint16_t destination_port, uint32_t sequence,
                               uint32_t acknowledgement, uint8_t flags,
                               uint16_t window, const uint8_t *data,
                               uint16_t length) {
    (void)window; (void)data; sent_ip = ip; sent_source_port = source_port;
    sent_destination_port = destination_port; sent_sequence = sequence;
    sent_acknowledgement = acknowledgement; sent_flags = flags;
    sent_length = length; pending_reply = true; return true;
}
void wait_queue_init(wait_queue_t *queue) { memset(queue, 0, sizeof(*queue)); }
bool wait_queue_wake_one_locked(wait_queue_t *queue) {
    (void)queue; return true;
}
size_t wait_queue_wake_all_locked(wait_queue_t *queue) {
    (void)queue; return 0U;
}
int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms) {
    (void)queue; (void)kind;
    if (pending_reply) {
        pending_reply = false;
        tcp_socket_segment_t reply = {
            .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(reply),
            .source_ip = sent_ip, .destination_ip = 0x0a000001U,
            .source_port = sent_destination_port,
            .destination_port = sent_source_port, .window = 4096U,
        };
        if ((sent_flags & TCP_FLAG_SYN) != 0U) {
            reply.sequence = 1000U; reply.acknowledgement = sent_sequence + 1U;
            reply.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
        } else {
            reply.sequence = sent_acknowledgement;
            reply.acknowledgement = sent_sequence + sent_length +
                (((sent_flags & TCP_FLAG_FIN) != 0U) ? 1U : 0U);
            reply.flags = TCP_FLAG_ACK;
            if ((sent_flags & TCP_FLAG_FIN) != 0U) reply.flags |= TCP_FLAG_FIN;
        }
        reply_result = tcp_socket_ingress(&reply, NULL);
        return 0;
    }
    now_ms = deadline_ms; return -110;
}

int main(void) {
    tcp_socket_init(); tcp_socket_handle_t socket = 0U;
    CHECK(tcp_socket_open(7, 2U, &socket) == 0);
    /* Force the local sequence space across UINT32_MAX during the data send. */
    now_ms = 0xfffffffeU ^ (7U << 16U) ^ 2U;
    CHECK(tcp_socket_connect(7, 2U, socket, 0x0a000002U, 80U, 2000U) == 0);
    const uint8_t request[] = {'G','E','T'};
    int send_result = tcp_socket_send(7, 2U, socket, request,
                                      sizeof(request), 2000U);
    if (send_result != 3)
        return reply_result != 0 ? (uint8_t)(-reply_result)
                                 : (uint8_t)(-send_result);
    tcp_socket_segment_t data_segment = {
        .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(data_segment),
        .source_ip = 0x0a000002U, .destination_ip = 0x0a000001U,
        .sequence = 1001U, .acknowledgement = sent_sequence + sent_length,
        .source_port = 80U, .destination_port = sent_source_port,
        .window = 4096U, .length = 2U, .flags = TCP_FLAG_ACK,
    };
    const uint8_t response[] = {'O','K'};
    pending_reply = false;
    CHECK(tcp_socket_ingress(&data_segment, response) == 0);
    uint8_t received[8];
    CHECK(tcp_socket_receive(7, 2U, socket, received, sizeof(received), 0U) == 2);
    CHECK(received[0] == 'O' && received[1] == 'K');
    CHECK(tcp_socket_close(7, 2U, socket, 2000U) == 0);
    CHECK(tcp_socket_receive(7, 2U, socket, received, sizeof(received), 0U) == -9);

    /* Exercise bounded passive open, backlog pressure and accepted I/O. */
    tcp_socket_handle_t listener = 0U;
    CHECK(tcp_socket_open(8, 3U, &listener) == 0);
    CHECK(tcp_socket_listen(8, 3U, listener, 8080U, 1U) == 0);
    CHECK(tcp_socket_accept(8, 3U, listener, &socket, &sent_ip,
                            &sent_destination_port, 0U) == -11);
    tcp_socket_segment_t syn = {
        .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(syn),
        .source_ip = 0x0a000003U, .destination_ip = 0x0a000001U,
        .sequence = 2000U, .source_port = 40000U,
        .destination_port = 8080U, .window = 4096U,
        .flags = TCP_FLAG_SYN,
    };
    CHECK(tcp_socket_ingress(&syn, NULL) == 0);
    CHECK(sent_ip == syn.source_ip && sent_source_port == 8080U &&
          sent_destination_port == syn.source_port &&
          sent_acknowledgement == syn.sequence + 1U &&
          sent_flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
    uint32_t server_isn = sent_sequence;
    pending_reply = false;
    tcp_socket_segment_t overflow_syn = syn;
    overflow_syn.source_ip = 0x0a000004U;
    overflow_syn.source_port = 40001U;
    CHECK(tcp_socket_ingress(&overflow_syn, NULL) == -105);

    tcp_socket_segment_t handshake = {
        .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(handshake),
        .source_ip = syn.source_ip, .destination_ip = syn.destination_ip,
        .sequence = syn.sequence + 1U, .acknowledgement = server_isn + 1U,
        .source_port = syn.source_port, .destination_port = syn.destination_port,
        .window = 4096U, .flags = TCP_FLAG_ACK,
    };
    const uint8_t get[] = {'G','E','T'};
    handshake.length = sizeof(get);
    handshake.flags |= TCP_FLAG_PSH;
    CHECK(tcp_socket_ingress(&handshake, get) == 0);
    uint32_t peer_ip = 0U; uint16_t peer_port = 0U;
    CHECK(tcp_socket_accept(8, 3U, listener, &socket, &peer_ip,
                            &peer_port, 0U) == 0);
    CHECK(peer_ip == syn.source_ip && peer_port == syn.source_port);

    CHECK(tcp_socket_receive(8, 3U, socket, received,
                             sizeof(received), 0U) == 3);
    CHECK(memcmp(received, get, sizeof(get)) == 0);
    const uint8_t ok[] = {'O','K'};
    CHECK(tcp_socket_send(8, 3U, socket, ok, sizeof(ok), 2000U) == 2);
    CHECK(tcp_socket_close(8, 3U, socket, 2000U) == 0);
    CHECK(tcp_socket_close(8, 3U, listener, 0U) == 0);
    return 0;
}
