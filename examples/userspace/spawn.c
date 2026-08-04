#include "x86os.h"

static void print_integer(int value) {
    char digits[11];
    unsigned count = 0;
    unsigned magnitude;
    if (value < 0) {
        x86os_putchar('-');
        magnitude = 0U - (unsigned)value;
    } else {
        magnitude = (unsigned)value;
    }
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        x86os_puts("Usage: spawn <program.prg>\n");
        return 2;
    }

    int child = x86os_spawn(argv[1]);
    if (child < 0) {
        x86os_puts("Unable to start child program.\n");
        return 1;
    }
    x86os_puts("Started PID ");
    print_integer(child);
    x86os_puts(" from PID ");
    print_integer(x86os_getpid());
    x86os_putchar('\n');

    int status = 0;
    if (x86os_wait(child, &status) < 0) {
        x86os_puts("Unable to wait for child process.\n");
        return 1;
    }
    x86os_puts("Child exited with status ");
    print_integer(status);
    x86os_putchar('\n');
    return status;
}
