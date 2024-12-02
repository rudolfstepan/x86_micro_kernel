#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

void main() {

    int counter = 0;

    printf("DIR CLI started\n");

    while (1)
    {
         //printf("DIR CLI running %d\n", counter++);

         //delay_ms(3000);

         asm volatile("int $0x29"); // Trigger a timer interrupt
    }
}