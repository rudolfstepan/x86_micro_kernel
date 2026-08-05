#include "x86os.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        x86os_puts("Usage: del <file>\n");
        return 2;
    }
    if (x86os_unlink(argv[1]) < 0) {
        x86os_puts("del: file not found or cannot be removed\n");
        return 1;
    }
    return 0;
}
