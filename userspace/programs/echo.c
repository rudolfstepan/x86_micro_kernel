/**
 * @file userspace/programs/echo.c
 * @brief Gibt Argumente auf der Konsole aus.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (index != 1) x86os_putchar(' ');
        x86os_puts(argv[index]);
    }
    x86os_putchar('\n');
    return 0;
}
