#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include <stdint.h>
#include "drivers/video/video.h"
#include "drivers/keyboard/keyboard.h"
#include "kernel/pit.h"


// uintptr_t get_syscall_table_address();
// int calculate(int a, int b);
// void test_delay_ms(int ms);

int start(void) {

    //clear_screen(BLACK);
    printf("Test Program started!\n");

    //uintptr_t syscall_table_addr = get_syscall_table_address();

    // if(syscall_table_addr == 0) {
    //     printf("Failed to get the syscall table address.\n");
    //     return 1;
    // }
    // Cast the address to a function pointer type if needed and use it
    //printf("Syscall table address: %u\n", syscall_table_addr);

    //sleep(1); // Wait for a short period of time (you'd use a timer interrupt in a real kernel)

    printf("Testing delay function...\n");
    //wait_for_enter();

    //delay(10); // Sleep for 1 second

    printf("Delay completed!\n");


    // printf("Calling the calculate function...\n");

    // int a = 5;
    // int b = 10;
    // int result = calculate(a, b);
    // printf("The result of %d + %d is %d\n", a, b, result);

    // printf("Program will now exit and return to the kernel.\n");

    // exit(0);

    return 0;
}

// int calculate(int a, int b) {
//     return a + b;
// }

// void test_delay_ms(int ms) {
//     sleep_ms(ms);

//     printf("Delay completed!\n");

//     return;
// }

//Function to invoke the system call
// uintptr_t get_syscall_table_address() {
//     uintptr_t address;
//     __asm__ __volatile__(
//         "int $0x80;" // Invoke interrupt 0x80
//         : "=a" (address) // The address will be returned in the EAX register
//         : "a" (0) // Pass 0 in the EAX register to request the syscall table address
//     );
//     return address;
// }