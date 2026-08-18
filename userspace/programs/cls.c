/**
 * @file userspace/programs/cls.c
 * @brief Löscht die Terminalanzeige.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(void) {
    x86os_clear();
    return 0;
}
