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
    uint32_t memory_kb = x86os_memory_kb();
    x86os_puts("Usable physical memory: ");
    print_unsigned(memory_kb / 1024U);
    x86os_puts(" MiB (");
    print_unsigned(memory_kb);
    x86os_puts(" KiB)\n");
    return 0;
}
