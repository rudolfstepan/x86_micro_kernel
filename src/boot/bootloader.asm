[BITS 32]
global start
global interrupt

extern stack_start
extern kernel_main

section .text
start:
    ; Initialize stack pointer
    mov esp, stack_start + 8192
    ; Call the kernel main function
    push ebx
    push dword 0x36d76289
    call kernel_main
    hlt
