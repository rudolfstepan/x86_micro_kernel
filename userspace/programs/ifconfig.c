/**
 * @file userspace/programs/ifconfig.c
 * @brief Zeigt oder setzt die IPv4-Konfiguration von net0.
 */
#include "x86os.h"

static void print_u32(uint32_t value) {
    char digits[10]; uint32_t count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}
static void print_ip(uint32_t ip) {
    for (uint32_t shift = 24U;; shift -= 8U) {
        print_u32((ip >> shift) & 0xffU);
        if (shift == 0U) break; x86os_putchar('.');
    }
}
static int parse_ip(const char *text, uint32_t *out) {
    uint32_t ip = 0U;
    if (text == 0 || out == 0) return -1;
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
static int status(void) {
    x86os_network_control_request_t request = {
        .version = X86OS_NETWORK_CONTROL_VERSION,
        .struct_size = sizeof(request), .operation = X86OS_NETWORK_STATUS,
    };
    x86os_network_control_result_t result;
    if (x86os_network_control(&request, &result) != 0) return 1;
    x86os_puts("net0: "); x86os_puts(result.ready ? "up" : "down");
    x86os_puts(" backend="); x86os_puts(result.backend);
    x86os_puts(" ip=");
    if (result.configured) print_ip(result.ip_address); else x86os_puts("none");
    x86os_puts(" mask="); print_ip(result.netmask);
    x86os_puts(" gateway="); print_ip(result.gateway); x86os_putchar('\n');
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 1) return status();
    if (argc != 4) {
        x86os_puts("usage: ifconfig [<ip> <netmask> <gateway>]\n"); return 2;
    }
    x86os_network_control_request_t request = {
        .version = X86OS_NETWORK_CONTROL_VERSION,
        .struct_size = sizeof(request), .operation = X86OS_NETWORK_CONFIGURE,
    };
    if (parse_ip(argv[1], &request.ip_address) != 0 ||
        parse_ip(argv[2], &request.netmask) != 0 ||
        parse_ip(argv[3], &request.gateway) != 0) {
        x86os_puts("ifconfig: invalid IPv4 address\n"); return 2;
    }
    x86os_network_control_result_t result;
    if (x86os_network_control(&request, &result) != 0 ||
        result.operation_result != 0) {
        x86os_puts("ifconfig: configuration failed\n"); return 1;
    }
    return status();
}
