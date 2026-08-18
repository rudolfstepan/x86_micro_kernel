/**
 * @file userspace/programs/touch.c
 * @brief Aktualisiert Zeitstempel oder legt eine leere Datei an.
 */
#include "x86os.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        x86os_puts("Usage: touch <file>\n");
        return 2;
    }
    x86os_file_info_t existing;
    if (x86os_stat(argv[1], &existing) == 0) {
        if (existing.type == X86OS_DIRECTORY)
            x86os_puts("touch: path is a directory\n");
        if (existing.type == X86OS_DIRECTORY) return 1;
        if (x86os_touch(argv[1]) < 0) {
            x86os_puts("touch: cannot update timestamps\n");
            return 1;
        }
        return 0;
    }
    int descriptor = x86os_create(argv[1]);
    if (descriptor < 0) {
        x86os_puts("touch: cannot create file\n");
        return 1;
    }
    int result = x86os_fsync(descriptor);
    if (x86os_close(descriptor) < 0) result = -1;
    if (result < 0) {
        x86os_puts("touch: close or sync failed\n");
        return 1;
    }
    return 0;
}
