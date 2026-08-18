/**
 * @file userspace/programs/fault_pf.c
 * @brief Injiziert einen Userspace-Seitenfehler.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(void) {
    *(volatile uint32_t*)(uintptr_t)0x00001000U = 0xA5A5A5A5U;
    return 77;
}
