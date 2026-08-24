/**
 * @file userspace/programs/cat.c
 * @brief Gibt eine Datei begrenzt aus.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"
#include "reist/vfs_read_client.h"

#define PATH_CAPACITY 256

static int read_path(char *path) {
    unsigned int length = 0;
    x86os_puts("File: ");
    for (;;) {
        char ch = (char)x86os_getchar();
        if (ch == '\r' || ch == '\n') {
            x86os_putchar('\n');
            break;
        }
        if (ch == '\b') {
            if (length != 0) {
                --length;
                x86os_puts("\b \b");
            }
        } else if (ch >= ' ' && ch <= '~' && length + 1U < PATH_CAPACITY) {
            path[length++] = ch;
            x86os_putchar(ch);
        }
    }
    path[length] = '\0';
    return length == 0 ? -1 : 0;
}

int main(int argc, char **argv) {
    char path[PATH_CAPACITY];
    char buffer[256];
    const char *requested_path;
    if (argc > 1) {
        requested_path = argv[1];
    } else {
        if (read_path(path) != 0) return 1;
        requested_path = path;
    }

    uint32_t offset = 0U;
    for (;;) {
        int count = reist_vfs_read_at(requested_path, offset, buffer,
                                      sizeof(buffer),
                                      REIST_VFS_READ_DEFAULT_TIMEOUT_MS);
        if (count < 0) {
            x86os_puts("cat: read error\n");
            return 1;
        }
        if (count == 0) break;
        for (int i = 0; i < count; ++i) x86os_putchar(buffer[i]);
        if (UINT32_MAX - offset < (uint32_t)count) {
            x86os_puts("cat: file is too large\n");
            return 1;
        }
        offset += (uint32_t)count;
    }
    return 0;
}
