#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

#include <stdint.h>
#include <stdbool.h>

TryContext ctx;

// Function to get the current value of ESP
uint32_t get_esp() {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp)); // Inline assembly to move ESP into a variable
    return esp;
}

// Function to get the current value of EBP
uint32_t get_ebp() {
    uint32_t ebp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp)); // Inline assembly to move EBP into a variable
    return ebp;
}

void main() __attribute__((section(".text.main")));
void main() {
    current_try_context = &ctx; // Set the global context pointer

    printf("Set current_try_context in user program: 0x%p\n", (void*)current_try_context);
    printf("Current ESP: 0x%X\n", get_esp());
    printf("Current EBP: 0x%X\n", get_ebp());

    if (setjmp(&ctx) == 0) {
        //int g = test_divide_by_zero();
        int x = 10;
        int y = 0;
        int z = x / y; // Trigger a divide-by-zero exception
        printf("z = %d\n", z);
        printf("Try block executed successfully\n");
    } else {
        printf("Caught divide-by-zero exception\n");
    }

    current_try_context = NULL; // Clear the context pointer
    printf("Program execution continues...\n");
}
