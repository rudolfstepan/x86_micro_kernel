/** @file userspace/programs/stat.c @brief Zeigt begrenzte Dateimetadaten. */
#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10];
    unsigned count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_timestamp(const char *label, uint32_t value) {
    x86os_puts(label);
    print_unsigned(value);
    x86os_puts(" (unix seconds)\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        x86os_puts("Usage: stat <path>\n");
        return 2;
    }
    x86os_file_info_t info;
    if (x86os_stat(argv[1], &info) < 0) {
        x86os_puts("stat: path not found\n");
        return 1;
    }
    x86os_puts("Name: "); x86os_puts(info.name); x86os_putchar('\n');
    x86os_puts("Type: ");
    x86os_puts(info.type == X86OS_DIRECTORY ? "directory" : "file");
    x86os_putchar('\n');
    x86os_puts("Size: "); print_unsigned(info.size); x86os_puts(" bytes\n");
    print_timestamp("Create time: ", info.create_time);
    print_timestamp("Modify time: ", info.modify_time);
    print_timestamp("Access time: ", info.access_time);
    return 0;
}
