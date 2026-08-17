#include "x86os.h"

static const char hex[] = "0123456789ABCDEF";

int main(void) {
    x86os_puts("Printable ASCII table\n");
    for (unsigned int value = 32; value < 127; ++value) {
        x86os_puts("0x");
        x86os_putchar(hex[(value >> 4) & 0x0f]);
        x86os_putchar(hex[value & 0x0f]);
        x86os_putchar(' ');
        x86os_putchar((char)value);
        x86os_puts(((value - 31U) % 8U) == 0U ? "\n" : "  ");
    }
    x86os_putchar('\n');
    return 0;
}
