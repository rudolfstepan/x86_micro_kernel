/** @file test/test_dns_host.c @brief Host checks for bounded DNS parsing. */
#include <stdint.h>
#include "x86os.h"

int reist_dns_parse_response(const uint8_t *, uint32_t, uint16_t,
                             const char *, uint32_t *, uint32_t *);
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)

int x86os_monotonic_ms(uint64_t *value) { *value = 0U; return -1; }
int x86os_network_control(const x86os_network_control_request_t *request,
                          x86os_network_control_result_t *result) {
    (void)request; (void)result; return -1;
}
int x86os_udp_socket_open(x86os_udp_socket_t *socket) {
    (void)socket; return -1;
}
int x86os_udp_socket_bind(x86os_udp_socket_t socket, uint16_t port) {
    (void)socket; (void)port; return -1;
}
int x86os_udp_socket_close(x86os_udp_socket_t socket) {
    (void)socket; return -1;
}
int x86os_getpid(void) { return 1; }
int x86os_udp_sendto(const x86os_udp_datagram_t *datagram, const void *data) {
    (void)datagram; (void)data; return -1;
}
int x86os_udp_recvfrom(x86os_udp_datagram_t *datagram, void *data) {
    (void)datagram; (void)data; return -1;
}
int x86os_sleep_ms(uint32_t milliseconds) { (void)milliseconds; return 0; }

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
    return 0;
}
