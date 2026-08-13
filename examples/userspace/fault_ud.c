#include "x86os.h"

int main(void) {
    __asm__ volatile("ud2");
    return 77;
}
