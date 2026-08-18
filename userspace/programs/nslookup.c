/** @file userspace/programs/nslookup.c @brief Resolves a DNS A/CNAME name. */
#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10]; uint32_t count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}
static void print_ip(uint32_t ip) {
    for (uint32_t shift = 24U;; shift -= 8U) {
        print_unsigned((ip >> shift) & 0xffU);
        if (shift == 0U) break; x86os_putchar('.');
    }
}
static int parse_ip(const char *text, uint32_t *result) {
    uint32_t address = 0U;
    for (uint32_t part = 0U; part < 4U; ++part) {
        uint32_t value = 0U, digits = 0U;
        while (*text >= '0' && *text <= '9') {
            value = value * 10U + (uint32_t)(*text++ - '0');
            if (++digits > 3U || value > 255U) return -1;
        }
        if (digits == 0U || (part < 3U && *text++ != '.')) return -1;
        address = (address << 8U) | value;
    }
    if (*text != '\0') return -1;
    *result = address; return 0;
}
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        x86os_puts("usage: nslookup <name> [server]\n"); return 2;
    }
    x86os_dns_result_t result;
    uint32_t server = 0U;
    if (argc == 3 && parse_ip(argv[2], &server) != 0) {
        x86os_puts("nslookup: invalid server\n"); return 2;
    }
    int rc = argc == 3 ? x86os_dns_resolve_at(argv[1], server, 3000U, &result)
                       : x86os_dns_resolve(argv[1], 3000U, &result);
    if (rc != 0) { x86os_puts("nslookup: resolution failed\n"); return 1; }
    x86os_puts("name: "); x86os_puts(result.canonical_name);
    x86os_puts("\naddress: "); print_ip(result.address);
    x86os_puts("\nttl: "); print_unsigned(result.ttl_seconds);
    if (result.from_cache) x86os_puts(" (cache)");
    x86os_putchar('\n'); return 0;
}
