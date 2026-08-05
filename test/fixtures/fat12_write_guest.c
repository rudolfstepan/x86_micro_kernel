#include "x86os.h"

static int same_bytes(const char* first, const char* second,
                      unsigned int size) {
    for (unsigned int index = 0; index < size; ++index) {
        if (first[index] != second[index]) return 0;
    }
    return 1;
}

int main(void) {
    static const char message[] = "persistent floppy data\n";
    char verify[sizeof(message)];

    int file = x86os_create("/TEST.TXT");
    if (file < 0 || x86os_write(file, message, sizeof(message) - 1U) !=
        (int)(sizeof(message) - 1U) || x86os_close(file) < 0) {
        x86os_puts("FAT12-WRITE-E2E-CREATE-FAILED\n");
        return 1;
    }
    file = x86os_open("/TEST.TXT");
    if (file < 0 || x86os_read(file, verify, sizeof(message) - 1U) !=
        (int)(sizeof(message) - 1U) || x86os_close(file) < 0 ||
        !same_bytes(message, verify, sizeof(message) - 1U)) {
        x86os_puts("FAT12-WRITE-E2E-READBACK-FAILED\n");
        return 2;
    }
    if (x86os_mkdir("/DATA") < 0) {
        x86os_puts("FAT12-WRITE-E2E-MKDIR-FAILED\n");
        return 3;
    }
    file = x86os_create("/DATA/COPY.TXT");
    if (file < 0 || x86os_write(file, message, sizeof(message) - 1U) !=
        (int)(sizeof(message) - 1U) || x86os_close(file) < 0) {
        x86os_puts("FAT12-WRITE-E2E-SUBDIR-FAILED\n");
        return 4;
    }
    if (x86os_unlink("/DATA/COPY.TXT") < 0 || x86os_rmdir("/DATA") < 0) {
        x86os_puts("FAT12-WRITE-E2E-DELETE-FAILED\n");
        return 5;
    }
    x86os_puts("FAT12-WRITE-E2E-OK\n");
    return 0;
}
