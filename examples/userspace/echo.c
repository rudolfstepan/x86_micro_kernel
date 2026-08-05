#include "x86os.h"

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (index != 1) x86os_putchar(' ');
        x86os_puts(argv[index]);
    }
    x86os_putchar('\n');
    return 0;
}
