#include "x86os.h"

int main(void) {
    char path[256];
    if (x86os_getcwd(path, sizeof(path)) < 0) {
        x86os_puts("Unable to read working directory.\n");
        return 1;
    }
    x86os_puts(path);
    x86os_putchar('\n');
    return 0;
}
