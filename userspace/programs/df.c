/** @file userspace/programs/df.c @brief Zeigt freien VFS-Speicher. */
#include "x86os.h"

static void print_unsigned(uint64_t value) {
    char digits[20];
    unsigned count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(int argc, char **argv) {
    if (argc > 2) {
        x86os_puts("Usage: df [path]\n");
        return 2;
    }
    const char *path = argc == 2 ? argv[1] : ".";
    x86os_space_info_t space;
    if (x86os_space(path, &space) < 0) {
        x86os_puts("df: filesystem space unavailable\n");
        return 1;
    }
    uint64_t used = space.total_bytes >= space.free_bytes
        ? space.total_bytes - space.free_bytes : 0U;
    x86os_puts("Filesystem  Total  Used  Free\n");
    x86os_puts(path); x86os_putchar(' ');
    print_unsigned(space.total_bytes); x86os_putchar(' ');
    print_unsigned(used); x86os_putchar(' ');
    print_unsigned(space.free_bytes); x86os_putchar('\n');
    return 0;
}
