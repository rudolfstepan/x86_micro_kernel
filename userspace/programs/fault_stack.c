#include "x86os.h"

/* The address is part of the versioned i386 userspace layout.  Touching the
 * lower guard proves that stack overflow is contained as a process #PF. */
#define USER_STACK_LOWER_GUARD 0xBFFF6000U

int main(void) {
    x86os_puts("FAULTSTK:ARMED\n");
    *(volatile uint32_t *)(uintptr_t)USER_STACK_LOWER_GUARD = 0x5354414BU;
    return 77;
}
