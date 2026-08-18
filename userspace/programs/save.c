/**
 * @file userspace/programs/save.c
 * @brief Schreibt Benutzerdaten in eine Datei.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

static unsigned int text_length(const char *text) {
    unsigned int length = 0;
    while (text[length] != '\0') ++length;
    return length;
}

static int write_all(int descriptor, const char *text) {
    unsigned int length = text_length(text);
    unsigned int offset = 0;
    while (offset < length) {
        int written = x86os_write(descriptor, text + offset, length - offset);
        if (written <= 0) return -1;
        offset += (unsigned int)written;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        x86os_puts("Usage: save <new-file> <text...>\n");
        return 1;
    }
    int descriptor = x86os_create(argv[1]);
    if (descriptor < 0) {
        x86os_puts("save: cannot create file (it may already exist)\n");
        return 1;
    }
    for (int i = 2; i < argc; ++i) {
        if (i != 2 && write_all(descriptor, " ") != 0) goto failed;
        if (write_all(descriptor, argv[i]) != 0) goto failed;
    }
    if (write_all(descriptor, "\n") != 0) goto failed;
    if (x86os_close(descriptor) < 0) return 1;
    return 0;

failed:
    (void)x86os_close(descriptor);
    x86os_puts("save: write error\n");
    return 1;
}
