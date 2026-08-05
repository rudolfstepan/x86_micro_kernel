#include "x86os.h"

static unsigned int text_length(const char *text) {
    unsigned int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static void print_spaces(unsigned int count) {
    while (count-- != 0U) x86os_putchar(' ');
}

static void print_unsigned(uint64_t value, unsigned int width) {
    char digits[20];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    if (width > count) print_spaces(width - count);
    while (count != 0U) x86os_putchar(digits[--count]);
}

static void print_name(const char *name) {
    unsigned int length = text_length(name);
    x86os_puts(name);
    if (length < 40U) print_spaces(40U - length);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    x86os_file_info_t directory;
    if (x86os_stat(path, &directory) < 0 ||
        directory.type != X86OS_DIRECTORY) {
        x86os_puts("ls: directory not found\n");
        return 1;
    }

    x86os_puts("\n Directory of ");
    x86os_puts(path);
    x86os_puts("\n\n");
    x86os_puts("NAME                                             SIZE\n");
    x86os_puts("-----------------------------------------------------\n");
    uint32_t file_count = 0;
    uint32_t directory_count = 0;
    uint64_t total_bytes = 0;
    for (uint32_t index = 0;;) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int result = x86os_readdir_batch(path, index, entries);
        if (result < 0) {
            x86os_puts("ls: read error\n");
            return 1;
        }
        if (result == 0) break;
        for (int batch_index = 0; batch_index < result; ++batch_index) {
            x86os_file_info_t *entry = &entries[batch_index];
            print_name(entry->name);
            if (entry->type == X86OS_DIRECTORY) {
                x86os_puts("        <DIR>");
                ++directory_count;
            } else {
                print_unsigned(entry->size, 12U);
                ++file_count;
                total_bytes += entry->size;
            }
            x86os_putchar('\n');
        }
        index += (uint32_t)result;
    }
    print_unsigned(file_count, 10U);
    x86os_puts(" File(s) ");
    print_unsigned(total_bytes, 0U);
    x86os_puts(" bytes\n");
    print_unsigned(directory_count, 10U);
    x86os_puts(" Dir(s)\n\n");
    return 0;
}
