/**
 * @file userspace/programs/mkdir.c
 * @brief Erstellt ein Verzeichnis.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        x86os_puts("Usage: mkdir <directory>\n");
        return 2;
    }
    if (x86os_mkdir(argv[1]) < 0) {
        x86os_puts("mkdir: unable to create directory\n");
        return 1;
    }
    return 0;
}
