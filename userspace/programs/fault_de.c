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
