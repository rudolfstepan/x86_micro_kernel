/* Actual legacy TCP state machine; only transport/clock/wait OS mocks. */
#define main legacy_tcp_main
#include "test/test_tcp_socket_host.c"
#undef main
#include <stdio.h>
#include "userspace/programs/network_cadence.h"
#undef CHECK
#define CHECK(v) do { if (!(v)) { fprintf(stderr, "line %d: %s\n", __LINE__, #v); return 1; } } while (0)

int main(void) {
    CHECK(legacy_tcp_main() == 0);
    tcp_socket_init(); now_ms = 1; reply_sequence = UINT32_MAX - 8192;
    tcp_socket_handle_t socket;
    CHECK(tcp_socket_open(7, 2, &socket) == 0);
    CHECK(tcp_socket_connect(7, 2, socket, 0x0a000002U, 80, 2000) == 0);
    CHECK(TCP_SOCKET_RECEIVE_CAPACITY == 2048 && TCP_SOCKET_MAX_SEGMENT == 512);
    CHECK(sent_window == 32768);
    tcp_socket_segment_t seg = {
        .version = TCP_SOCKET_ABI_VERSION, .struct_size = sizeof(seg),
        .source_ip = 0x0a000002U, .destination_ip = 0x0a000001U,
        .sequence = reply_sequence + 1, .acknowledgement = sent_sequence,
        .source_port = 80, .destination_port = sent_source_port,
        .window = 4096, .length = 512, .flags = TCP_FLAG_ACK,
    };
    uint8_t data[512], output[2048]; uint32_t produced = 0, consumed = 0;
    /* Many wraps of the real ring, including the peer's sequence wrap. */
    for (uint32_t cycle = 0; cycle < 40; ++cycle) {
        uint32_t fill = cycle == 0 ? 32768 : 14336;
        for (uint32_t i = 0; i < fill; i += 512) {
            for (uint32_t j = 0; j < 512; ++j)
                data[j] = (uint8_t)((produced + j) * 29U + (produced + j) / 251U);
            CHECK(tcp_socket_ingress(&seg, data) == 0);
            produced += 512; seg.sequence += 512;
            CHECK(sent_acknowledgement == seg.sequence);
            CHECK(sent_window == 32768 - (produced - consumed));
        }
        CHECK(sent_window == 0);
        uint32_t ack = sent_acknowledgement;
        CHECK(tcp_socket_ingress(&seg, data) == 0); /* full: drop, no publish */
        CHECK(sent_window == 0 && sent_acknowledgement == ack);
        CHECK(tcp_socket_receive(8, 2, socket, output, 2048, 0) == -9);
        CHECK(tcp_socket_receive(7, 3, socket, output, 2048, 0) == -9);
        CHECK(tcp_socket_receive(7, 2, socket, output, 2049, 0) == -22);
        for (uint32_t i = 0; i < 14336; i += 2048) {
            CHECK(tcp_socket_receive(7, 2, socket, output, 2048, 0) == 2048);
            for (uint32_t j = 0; j < 2048; ++j)
                CHECK(output[j] == (uint8_t)((consumed + j) * 29U + (consumed + j) / 251U));
            consumed += 2048;
            CHECK(sent_window == 32768 - (produced - consumed));
        }
    }
    seg.flags |= TCP_FLAG_FIN; seg.length = 0;
    CHECK(tcp_socket_ingress(&seg, NULL) == 0);
    while (consumed < produced) {
        int n = tcp_socket_receive(7, 2, socket, output, 2048, 0);
        CHECK(n > 0 && n <= 2048);
        for (int j = 0; j < n; ++j)
            CHECK(output[j] == (uint8_t)((consumed + (uint32_t)j) * 29U + (consumed + (uint32_t)j) / 251U));
        consumed += (uint32_t)n;
    }
    CHECK(tcp_socket_receive(7, 2, socket, output, 2048, 0) == 0);
    tcp_socket_process_cleanup(7, 3); /* stale owner cannot revoke */
    CHECK(tcp_socket_receive(7, 2, socket, output, 2048, 0) == 0);
    tcp_socket_process_cleanup(7, 2); tcp_socket_process_cleanup(7, 2);
    CHECK(tcp_socket_receive(7, 2, socket, output, 2048, 0) == -9);
    tcp_socket_handle_t fresh[4], extra;
    for (int i = 0; i < 4; ++i) CHECK(tcp_socket_open(7, 3, &fresh[i]) == 0);
    CHECK(tcp_socket_open(7, 3, &extra) == -28 && fresh[0] != socket);
    CHECK(tcp_socket_connect(7, 3, fresh[0], 0x0a000002U, 80, 2000) == 0);
    pending_reply = false;
    uint64_t start = now_ms;
    CHECK(tcp_socket_receive(7, 3, fresh[0], output, 2048, 17) == -110);
    CHECK(now_ms == start + 17);
    seg.destination_port = sent_source_port; seg.flags = TCP_FLAG_RST;
    CHECK(tcp_socket_ingress(&seg, NULL) == 0);
    CHECK(tcp_socket_receive(7, 3, fresh[0], output, 2048, 0) == 0);
    tcp_socket_process_cleanup(7, 3);
    tcp_socket_stats_t stats; tcp_socket_get_stats(&stats);
    CHECK(stats.active_sockets == 0 && stats.received_bytes == 0);

    network_cadence_t cadence = {0};
    CHECK(network_control_wait(&cadence, 0, false) == 40);
    CHECK(network_control_wait(&cadence, 100, true) == 1);
    CHECK(network_control_wait(&cadence, 199, false) == 1);
    CHECK(network_control_wait(&cadence, 200, false) == 40);
    CHECK(network_control_wait(&cadence, 201, true) == 1);
    CHECK(network_control_wait(&cadence, 99, true) == 40);
    CHECK(network_control_wait(&cadence, 100, false) == 40);
    CHECK(network_control_wait(&cadence, UINT64_MAX - 50, true) == 1);
    CHECK(network_control_wait(&cadence, UINT64_MAX, false) == 40);
    CHECK(network_control_wait(&cadence, 0, true) == 40);
    puts("NETWORK_PROGRESS_HOST_OK");
    return 0;
}
