/**
 * @file userspace/programs/udp.c
 * @brief Sendet oder empfängt ein begrenztes IPv4/UDP-Datagramm.
 */
#include "x86os.h"

static int equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int parse_u16(const char *text, uint16_t *out) {
    uint32_t value = 0U;
    if (text == 0 || out == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 65535U) return -1;
    }
    if (value == 0U) return -1; *out = (uint16_t)value; return 0;
}
static int parse_u32(const char *text, uint32_t *out) {
    uint32_t value = 0U;
    if (text == 0 || out == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        if (*text < '0' || *text > '9' ||
            value > (UINT32_MAX - (uint32_t)(*text - '0')) / 10U) return -1;
        value = value * 10U + (uint32_t)(*text++ - '0');
    }
    *out = value; return 0;
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
static uint32_t text_length(const char *text) {
    uint32_t length = 0U;
    while (text[length] != '\0' && length <= X86OS_UDP_MAX_DATAGRAM) ++length;
    return length;
}
static int bind_socket(uint16_t port, x86os_udp_socket_t *socket) {
    int result = x86os_udp_socket_open(socket);
    if (result == 0) result = x86os_udp_socket_bind(*socket, port);
    if (result != 0 && *socket != 0U) (void)x86os_udp_socket_close(*socket);
    return result;
}
int main(int argc, char **argv) {
    if (argc == 6 && equal(argv[1], "send")) {
        uint32_t ip = 0U; uint16_t remote = 0U, local = 0U;
        uint32_t length = text_length(argv[5]);
        if (parse_ip(argv[2], &ip) != 0 || parse_u16(argv[3], &remote) != 0 ||
            parse_u16(argv[4], &local) != 0 || length > X86OS_UDP_MAX_DATAGRAM)
            goto usage;
        x86os_udp_socket_t socket = 0U;
        if (bind_socket(local, &socket) != 0) {
            x86os_puts("udp: bind failed\n"); return 1;
        }
        x86os_udp_datagram_t datagram = {
            .version = X86OS_UDP_SOCKET_VERSION,
            .struct_size = sizeof(datagram), .socket = socket, .ip = ip,
            .destination_port = remote, .length = length,
            .timeout_ms = 2000U,
        };
        int result = x86os_udp_sendto(&datagram, argv[5]);
        (void)x86os_udp_socket_close(socket);
        if (result < 0) { x86os_puts("udp: send failed\n"); return 1; }
        x86os_puts("datagram sent\n"); return 0;
    }
    if ((argc == 3 || argc == 4) && equal(argv[1], "recv")) {
        uint16_t local = 0U; uint32_t timeout = 1000U;
        if (parse_u16(argv[2], &local) != 0 ||
            (argc == 4 && parse_u32(argv[3], &timeout) != 0)) goto usage;
        x86os_udp_socket_t socket = 0U;
        if (bind_socket(local, &socket) != 0) {
            x86os_puts("udp: bind failed\n"); return 1;
        }
        uint8_t data[X86OS_UDP_MAX_DATAGRAM];
        x86os_udp_datagram_t datagram = {
            .version = X86OS_UDP_SOCKET_VERSION,
            .struct_size = sizeof(datagram), .socket = socket,
            .length = sizeof(data), .timeout_ms = timeout,
        };
        int result = x86os_udp_recvfrom(&datagram, data);
        (void)x86os_udp_socket_close(socket);
        if (result < 0) {
            x86os_puts(result == -110 ? "udp: receive timed out\n" :
                                      "udp: receive failed\n");
            return 1;
        }
        for (uint32_t index = 0U; index < datagram.length; ++index)
            x86os_putchar(data[index] >= 32U && data[index] < 127U
                              ? (char)data[index] : '.');
        x86os_putchar('\n'); return 0;
    }
usage:
    x86os_puts("usage: udp send <ip> <port> <local-port> <text>\n"
               "       udp recv <local-port> [timeout-ms]\n");
    return 2;
}
