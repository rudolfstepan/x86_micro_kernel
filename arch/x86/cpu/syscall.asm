[BITS 32]
global syscall_handler_asm
extern syscall_handler

section .text

syscall_handler_asm:
    sti                     ; Enable interrupts during syscall handler
    call syscall_handler    ; Call the actual C handler function
    cli                     ; Disable interrupts before returning
    iretd                   ; Return from interrupt        