#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(void) {
    uint32_t seconds = x86os_uptime_ms() / 1000U;
    uint32_t days = seconds / 86400U;
    uint32_t hours = (seconds / 3600U) % 24U;
    uint32_t minutes = (seconds / 60U) % 60U;
    seconds %= 60U;
    x86os_puts("Uptime: ");
    print_unsigned(days);
    x86os_puts("d ");
    print_unsigned(hours);
    x86os_puts("h ");
    print_unsigned(minutes);
    x86os_puts("m ");
    print_unsigned(seconds);
    x86os_puts("s\n");
    return 0;
}
