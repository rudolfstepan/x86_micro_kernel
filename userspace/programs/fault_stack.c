/**
 * @file userspace/programs/fault_stack.c
 * @brief Injiziert einen Userspace-Stackfault.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

/* The address is part of the versioned i386 userspace layout.  Touching the
 * lower guard proves that stack overflow is contained as a process #PF. */
#define USER_STACK_LOWER_GUARD 0xBFFF6000U

int main(void) {
    x86os_puts("FAULTSTK:ARMED\n");
    *(volatile uint32_t *)(uintptr_t)USER_STACK_LOWER_GUARD = 0x5354414BU;
    return 77;
}
