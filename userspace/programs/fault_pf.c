#include "x86os.h"

int main(void) {
    *(volatile uint32_t*)(uintptr_t)0x00001000U = 0xA5A5A5A5U;
    return 77;
}
