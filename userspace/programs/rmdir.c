#include "x86os.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        x86os_puts("Usage: rmdir <directory>\n");
        return 2;
    }
    if (x86os_rmdir(argv[1]) < 0) {
        x86os_puts("rmdir: directory is not empty or cannot be removed\n");
        return 1;
    }
    return 0;
}
