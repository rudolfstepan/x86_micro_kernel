[BITS 32]
global syscall_handler_asm
extern syscall_handler

section .text
syscall_handler_asm:
    call syscall_handler    ; Call the C handler
    iretd                   ; Return from interrupt
