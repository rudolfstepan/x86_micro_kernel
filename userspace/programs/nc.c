/** @file userspace/programs/nc.c @brief Bounded TCP client diagnostic tool. */
#include "x86os.h"

static int parse_port(const char *text, uint16_t *out) {
    uint32_t value = 0U;
    if (text == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 65535U) return -1;
    }
    if (value == 0U) return -1; *out = (uint16_t)value; return 0;
}
static int parse_ip(const char *text, uint32_t *out) {
    uint32_t ip = 0U;
    for (uint32_t part = 0U; part < 4U; ++part) {
        uint32_t value = 0U, digits = 0U;
        while (*text >= '0' && *text <= '9') {
            value = value * 10U + (uint32_t)(*text++ - '0');
            if (++digits > 3U || value > 255U) return -1;
        }
        if (digits == 0U || (part < 3U && *text++ != '.') ||
            (part == 3U && *text != '\0')) return -1;
        ip = (ip << 8U) | value;
    }
    *out = ip; return 0;
}
static uint32_t length(const char *text) {
    uint32_t count = 0U;
    while (text[count] != '\0' && count <= X86OS_TCP_MAX_SEGMENT) ++count;
    return count;
}
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        x86os_puts("usage: nc <host|ipv4> <port> [text]\n"); return 2;
    }
    uint32_t ip = 0U; uint16_t port = 0U;
    if (parse_port(argv[2], &port) != 0) return 2;
    if (parse_ip(argv[1], &ip) != 0) {
        x86os_dns_result_t dns;
        if (x86os_dns_resolve(argv[1], 3000U, &dns) != 0) {
            x86os_puts("nc: name resolution failed\n"); return 1;
        }
        ip = dns.address;
    }
    x86os_tcp_socket_t socket = 0U;
    if (x86os_tcp_socket_open(&socket) != 0) {
        x86os_puts("nc: socket unavailable\n"); return 1;
    }
    x86os_tcp_connect_t connect = {
        .version = X86OS_TCP_SOCKET_VERSION, .struct_size = sizeof(connect),
        .socket = socket, .destination_ip = ip, .destination_port = port,
        .timeout_ms = 5000U,
    };
    int rc = x86os_tcp_connect(&connect);
    if (rc == 0 && argc == 4) {
        uint32_t amount = length(argv[3]);
        if (amount > X86OS_TCP_MAX_SEGMENT) rc = -90;
        else {
            x86os_tcp_io_t send = {
                .version = X86OS_TCP_SOCKET_VERSION,
                .struct_size = sizeof(send), .socket = socket,
                .length = amount, .timeout_ms = 5000U,
            };
            rc = x86os_tcp_send(&send, argv[3]);
        }
    }
    if (rc >= 0) {
        uint8_t buffer[X86OS_TCP_RECEIVE_CAPACITY];
        x86os_tcp_io_t receive = {
            .version = X86OS_TCP_SOCKET_VERSION,
            .struct_size = sizeof(receive), .socket = socket,
            .length = sizeof(buffer), .timeout_ms = 3000U,
        };
        rc = x86os_tcp_receive(&receive, buffer);
        if (rc > 0) {
            for (uint32_t index = 0U; index < receive.length; ++index)
                x86os_putchar((char)buffer[index]);
            if (buffer[receive.length - 1U] != '\n') x86os_putchar('\n');
        } else if (rc == -110) rc = 0;
    }
    (void)x86os_tcp_socket_close(socket, 2000U);
    if (rc < 0) { x86os_puts("nc: connection failed\n"); return 1; }
    return 0;
}
