/**
 * @file userspace/programs/sleep_child.c
 * @brief Testprogramm für Schlafen und Kind-Lifecycle.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(void) {
    return x86os_sleep_ms(400) == 0 ? 41 : 77;
}
