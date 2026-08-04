#include "x86os.h"

static void print_two(uint32_t value) {
    x86os_putchar((char)('0' + (value / 10U) % 10U));
    x86os_putchar((char)('0' + value % 10U));
}

static void print_year(uint32_t year) {
    x86os_putchar((char)('0' + (year / 1000U) % 10U));
    x86os_putchar((char)('0' + (year / 100U) % 10U));
    x86os_putchar((char)('0' + (year / 10U) % 10U));
    x86os_putchar((char)('0' + year % 10U));
}

int main(void) {
    uint32_t date = x86os_get_date();
    uint32_t time = x86os_get_time();
    print_year(date >> 16);
    x86os_putchar('-');
    print_two((date >> 8) & 0xffU);
    x86os_putchar('-');
    print_two(date & 0xffU);
    x86os_putchar(' ');
    print_two((time >> 16) & 0xffU);
    x86os_putchar(':');
    print_two((time >> 8) & 0xffU);
    x86os_putchar(':');
    print_two(time & 0xffU);
    x86os_putchar('\n');
    return 0;
}
