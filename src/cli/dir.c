#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

void main() {

    int counter = 0;

    while (1)
    {
         printf("DIR CLI running %d\n", counter++);

         sleep_ms(1000);

         asm volatile("int $0x29"); // Trigger a timer interrupt
    }
}