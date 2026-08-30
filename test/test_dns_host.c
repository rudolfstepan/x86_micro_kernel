/** @file test/test_dns_host.c @brief Host checks for bounded DNS parsing. */
#include <stdint.h>
#include <string.h>
#include "x86os.h"

int reist_dns_parse_response(const uint8_t *, uint32_t, uint16_t,
                             const char *, uint32_t *, uint32_t *);
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

static uint64_t host_now = 1000U;
static uint8_t tcp_stream[X86OS_UDP_MAX_DATAGRAM + 2U];
static uint32_t tcp_stream_length;
static uint32_t tcp_stream_offset;
static uint32_t udp_queries;
static uint32_t tcp_queries;

int x86os_monotonic_ms(uint64_t *value) {
    if (value == 0) return -22;
    *value = host_now; host_now += 10U; return 0;
}
int x86os_network_control(const x86os_network_control_request_t *request,
                          x86os_network_control_result_t *result) {
    (void)request; (void)result; return -1;
}
int x86os_udp_socket_open(x86os_udp_socket_t *socket) {
    if (socket == 0) return -22;
    *socket = 1U;
    return 0;
}
int x86os_udp_socket_bind(x86os_udp_socket_t socket, uint16_t port) {
    return socket == 1U && port >= 49152U ? 0 : -22;
}
int x86os_udp_socket_close(x86os_udp_socket_t socket) {
    return socket == 1U ? 0 : -22;
}
int x86os_getpid(void) { return 1; }
int x86os_udp_sendto(const x86os_udp_datagram_t *datagram, const void *data) {
    if (datagram == 0 || data == 0 || datagram->destination_port != 53U)
        return -22;
    ++udp_queries; return (int)datagram->length;
}
int x86os_udp_recvfrom(x86os_udp_datagram_t *datagram, void *data) {
    (void)datagram; (void)data; return -110;
}
int x86os_tcp_socket_open(x86os_tcp_socket_t *socket) {
    if (socket == 0) return -22;
    *socket = 2U;
    return 0;
}
int x86os_tcp_connect(const x86os_tcp_connect_t *request) {
    return request != 0 && request->socket == 2U &&
           request->destination_ip == 0x0a000203U &&
           request->destination_port == 53U && request->timeout_ms != 0U
        ? 0 : -22;
}
int x86os_tcp_send(const x86os_tcp_io_t *request, const void *data) {
    if (request == 0 || data == 0 || request->socket != 2U ||
        request->length < 14U || request->length > sizeof(tcp_stream))
        return -22;
    const uint8_t *framed = (const uint8_t *)data;
    uint32_t query_length = ((uint32_t)framed[0] << 8U) | framed[1];
    if (query_length + 2U != request->length || query_length + 18U >
        sizeof(tcp_stream)) return -22;
    uint32_t response_length = query_length + 16U;
    tcp_stream[0] = (uint8_t)(response_length >> 8U);
    tcp_stream[1] = (uint8_t)response_length;
    memcpy(tcp_stream + 2U, framed + 2U, query_length);
    uint8_t *response = tcp_stream + 2U;
    response[2] = 0x81U; response[3] = 0x80U;
    response[6] = 0U; response[7] = 1U;
    response[8] = 0U; response[9] = 0U;
    response[10] = 0U; response[11] = 0U;
    uint8_t answer[] = {
        0xc0U,0x0cU, 0U,1U, 0U,1U, 0U,0U,0U,60U, 0U,4U,
        142U,250U,1U,1U};
    memcpy(response + query_length, answer, sizeof(answer));
    tcp_stream_length = response_length + 2U;
    tcp_stream_offset = 0U; ++tcp_queries;
    return (int)request->length;
}
int x86os_tcp_receive(x86os_tcp_io_t *request, void *data) {
    if (request == 0 || data == 0 || request->socket != 2U ||
        tcp_stream_offset >= tcp_stream_length) return -84;
    uint32_t amount = tcp_stream_length - tcp_stream_offset;
    if (amount > request->length) amount = request->length;
    if (amount > 3U) amount = 3U;
    memcpy(data, tcp_stream + tcp_stream_offset, amount);
    tcp_stream_offset += amount; request->length = amount;
    return (int)amount;
}
int x86os_tcp_socket_close(x86os_tcp_socket_t socket, uint32_t timeout_ms) {
    return socket == 2U && timeout_ms != 0U ? 0 : -22;
}

int main(void) {
    /* q.example CNAME alias.example, followed by its A record. */
    const uint8_t response[] = {
        0x12,0x34, 0x81,0x80, 0,1, 0,2, 0,0, 0,0,
        1,'q',7,'e','x','a','m','p','l','e',0, 0,1, 0,1,
        0xc0,0x0c, 0,5, 0,1, 0,0,0,30, 0,8,
        5,'a','l','i','a','s',0xc0,0x0e,
        0xc0,0x27, 0,1, 0,1, 0,0,0,20, 0,4, 192,0,2,10,
    };
    uint32_t address = 0U, ttl = 0U;
    CHECK(reist_dns_parse_response(response, sizeof(response), 0x1234U,
                                   "q.example", &address, &ttl) == 0);
    CHECK(address == 0xc000020aU && ttl == 20U);
    uint8_t loop[] = {
        0x12,0x34, 0x81,0x80, 0,1, 0,1, 0,0, 0,0,
        0xc0,0x0c, 0,1,0,1,
    };
    CHECK(reist_dns_parse_response(loop, sizeof(loop), 0x1234U,
                                   "q.example", &address, &ttl) == -74);
    x86os_dns_result_t resolved;
    CHECK(x86os_dns_resolve_at("google.com", 0x0a000203U, 3000U,
                               &resolved) == 0);
    CHECK(resolved.address == 0x8efa0101U && resolved.ttl_seconds == 60U);
    CHECK(udp_queries == 1U && tcp_queries == 1U);
    return 0;
}
