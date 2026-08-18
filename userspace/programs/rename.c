/**
 * @file userspace/programs/rename.c
 * @brief Benennt eine Datei innerhalb eines VFS-Mounts um.
 *
 * Der Kernel entscheidet weiterhin atomar über Filesystem- und Mountgrenzen.
 * Das Programm verweigert ein vorhandenes Ziel bewusst vor dem Syscall.
 */
#include "x86os.h"

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        x86os_puts("Usage: rename <old-path> <new-path>\n");
        return 2;
    }
    if (text_equal(argv[1], argv[2])) {
        x86os_puts("rename: source and destination are identical\n");
        return 1;
    }
    x86os_file_info_t source;
    if (x86os_stat(argv[1], &source) < 0) {
        x86os_puts("rename: source not found\n");
        return 1;
    }
    if (source.type == X86OS_DIRECTORY) {
        x86os_puts("rename: directories are not supported by this filesystem path\n");
        return 1;
    }
    x86os_file_info_t destination;
    if (x86os_stat(argv[2], &destination) == 0) {
        x86os_puts("rename: destination already exists\n");
        return 1;
    }
    if (x86os_rename(argv[1], argv[2]) < 0) {
        x86os_puts("rename: operation unsupported or failed\n");
        return 1;
    }
    return 0;
}
