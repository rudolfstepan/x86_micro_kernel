/**
 * @file userspace/programs/ping.c
 * @brief Sendet einen begrenzten ICMP-Echo-Test an eine IPv4-Adresse.
 */
#include "x86os.h"

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

int main(int argc, char **argv) {
    uint32_t target = 0U;
    if (argc != 2 || parse_ip(argv[1], &target) != 0 || target == 0U) {
        x86os_puts("usage: ping <ipv4-address>\n"); return 2;
    }
    x86os_network_control_request_t request = {
        .version = X86OS_NETWORK_CONTROL_VERSION,
        .struct_size = sizeof(request), .operation = X86OS_NETWORK_PING,
        .target_ip = target, .identifier = 0x5245U, .sequence = 1U,
        .timeout_ms = 1000U,
    };
    x86os_network_control_result_t result = {0};
    int rc = x86os_network_control(&request, &result);
    if (rc == 0 && result.operation_result == 0) {
        x86os_puts("reply received\n"); return 0;
    }
    x86os_puts(rc == -110 || result.operation_result == -110
                   ? "request timed out\n" : "ping failed\n");
    return 1;
}
