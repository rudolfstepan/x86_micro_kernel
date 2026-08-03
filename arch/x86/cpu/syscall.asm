[BITS 32]
global syscall_handler_asm
extern syscall_handler

section .text

syscall_handler_asm:
    ; Build the same Registers frame used by IRQ/exception stubs.
    push dword 0            ; Synthetic error code
    push dword 0x80         ; Interrupt/syscall vector
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld

    ; Only permit nested hardware interrupts if the caller had IF set.
    test dword [esp + 64], 0x200
    jz .interrupts_ready
    sti
.interrupts_ready:
    mov ebx, esp
    and esp, 0xfffffff0
    sub esp, 12
    push ebx
    call syscall_handler
    mov esp, ebx

    cli
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iretd
