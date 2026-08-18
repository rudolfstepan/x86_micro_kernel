/**
 * @file userspace/programs/fault_de.c
 * @brief Injiziert eine Division-durch-null-Ausnahme.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

int main(void) {
    __asm__ volatile(
        "xorl %%edx, %%edx\n"
        "movl $1, %%eax\n"
        "xorl %%ecx, %%ecx\n"
        "divl %%ecx\n"
        ::: "eax", "ecx", "edx", "cc");
    return 77;
}
