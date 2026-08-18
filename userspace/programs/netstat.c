/**
 * @file userspace/programs/netstat.c
 * @brief Zeigt den Zustand der begrenzten REIST-Netzwerkschnittstelle.
 */
#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10]; uint32_t count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_ip(uint32_t ip) {
    for (uint32_t shift = 24U;; shift -= 8U) {
        uint32_t value = (ip >> shift) & 0xffU;
        char digits[3]; uint32_t count = 0U;
        do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
        while (value != 0U);
        while (count != 0U) x86os_putchar(digits[--count]);
        if (shift == 0U) break;
        x86os_putchar('.');
    }
}

int main(void) {
    x86os_network_control_request_t request = {
        .version = X86OS_NETWORK_CONTROL_VERSION,
        .struct_size = sizeof(request), .operation = X86OS_NETWORK_STATUS,
    };
    x86os_network_control_result_t result;
    if (x86os_network_control(&request, &result) != 0) {
        x86os_puts("netstat: network service unavailable\n"); return 1;
    }
    x86os_puts("interface  backend  state       address\nnet0       ");
    x86os_puts(result.backend[0] != '\0' ? result.backend : "none");
    x86os_puts(result.ready ? (result.configured ? "  configured  " :
                              "  unconfigured  ") : "  down        ");
    if (result.configured) print_ip(result.ip_address); else x86os_putchar('-');
    x86os_putchar('\n');
    x86os_udp_socket_control_t sockets;
    if (x86os_udp_socket_stats(&sockets) == 0) {
        x86os_puts("udp sockets="); print_unsigned(sockets.active_sockets);
        x86os_puts(" queued="); print_unsigned(sockets.queued_datagrams);
        x86os_puts(" dropped="); print_unsigned(sockets.dropped_datagrams);
        x86os_putchar('\n');
    }
    x86os_tcp_socket_control_t tcp;
    if (x86os_tcp_socket_stats(&tcp) == 0) {
        x86os_puts("tcp sockets="); print_unsigned(tcp.active_sockets);
        x86os_puts(" established="); print_unsigned(tcp.established_sockets);
        x86os_puts(" retransmissions="); print_unsigned(tcp.retransmissions);
        x86os_putchar('\n');
    }
    return result.ready ? 0 : 1;
}
