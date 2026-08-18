/**
 * @file userspace/programs/copy.c
 * @brief Kopiert eine Datei mit begrenzten Puffern.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        x86os_puts("Usage: copy <source> <destination>\n");
        return 2;
    }
    int source = x86os_open(argv[1]);
    if (source < 0) {
        x86os_puts("copy: source file not found\n");
        return 1;
    }
    int destination = x86os_create(argv[2]);
    if (destination < 0) {
        (void)x86os_close(source);
        x86os_puts("copy: destination already exists or cannot be created\n");
        return 1;
    }

    char buffer[512];
    int failed = 0;
    for (;;) {
        int amount = x86os_read(source, buffer, sizeof(buffer));
        if (amount < 0) { failed = 1; break; }
        if (amount == 0) break;
        int offset = 0;
        while (offset < amount) {
            int written = x86os_write(destination, buffer + offset,
                                      (size_t)(amount - offset));
            if (written <= 0) { failed = 1; break; }
            offset += written;
        }
        if (failed) break;
    }
    if (x86os_close(source) < 0) failed = 1;
    if (x86os_close(destination) < 0) failed = 1;
    if (failed) {
        x86os_puts("copy: I/O error\n");
        return 1;
    }
    x86os_puts("        1 file(s) copied.\n");
    return 0;
}
