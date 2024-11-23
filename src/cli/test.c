#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

//TryContext ctx;

void sub() {
    int x = 10;
    int y = 2;
    int z = x / y; // Trigger divide-by-zero exception
    printf("z = %d\n", z);
}

void main() __attribute__((section(".text.main"))); // Set the entry point to the main function
void main()
 {
    //current_try_context = &ctx; // Set the global context pointer

    // printf("Set ctx in user program: 0x%p\n", &ctx);
    // printf("Set current_try_context in user program: 0x%p\n", (void*)current_try_context);
    // printf("Current ESP: 0x%X\n", get_esp());
    // printf("Current EBP: 0x%X\n", get_ebp());

    // if (setjmp(&ctx) == 0) {
    //     int x = 10;
    //     int y = 0;
    //     int z = x / y; // Trigger divide-by-zero exception
    //     printf("z = %d\n", z);
    //     printf("Try block executed successfully\n");
    // } else {
    //     printf("Caught divide-by-zero exception\n");
    // }

    // current_try_context = NULL; // Clear the context pointer

    //sub();

    int counter = 0;

    while (1==1)
    {
        sleep_ms(500);

        printf("TEST CLI running %d\n", counter++);
    }
}