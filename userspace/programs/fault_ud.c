/**
 * @file userspace/programs/fault_ud.c
 * @brief Injiziert eine Invalid-Opcode-Userspace-Ausnahme.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(void) {
    __asm__ volatile("ud2");
    return 77;
}
