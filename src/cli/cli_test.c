#include "../toolchain/stdio.h"


//extern void sleep(int ticks);

uintptr_t get_syscall_table_address();


int start(void) {

    clear_screen(BLACK);
    printf("Test Program started!\n");

    uintptr_t syscall_table_addr = get_syscall_table_address();

    // if(syscall_table_addr == 0) {
    //     printf("Failed to get the syscall table address.\n");
    //     return 1;
    // }
    // Cast the address to a function pointer type if needed and use it
    printf("Syscall table address: %u\n", syscall_table_addr);

    //sleep(1); // Wait for a short period of time (you'd use a timer interrupt in a real kernel)

    printf("Program will now exit and return to the kernel.\n");

    return 0;
}

//Function to invoke the system call
uintptr_t get_syscall_table_address() {
    uintptr_t address;
    __asm__ __volatile__(
        "int $0x80;" // Invoke interrupt 0x80
        : "=a" (address) // The address will be returned in the EAX register
        : "a" (0) // Pass 0 in the EAX register to request the syscall table address
    );
    return address;
}