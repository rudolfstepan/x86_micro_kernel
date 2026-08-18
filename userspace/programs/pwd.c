/**
 * @file userspace/programs/pwd.c
 * @brief Zeigt das aktuelle Arbeitsverzeichnis.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
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
