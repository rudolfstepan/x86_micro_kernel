bits 16

section .multiboot
    dd 0x1BADB002            ; Magic number
    dd 0x0                   ; Flags
    dd - (0x1BADB002 + 0x0)  ; Checksum

section .text
global start
extern main                  ; External symbol defined in C

start:
    cli                      ; Disable interrupts
    lgdt [gdt_descriptor]    ; Load the GDT

    ; Enable A20 line (optional)
    ;call enable_a20

    mov eax, cr0
    or eax, 0x1              ; Set PE bit
    mov cr0, eax

    jmp CODE_SEG:start_protected_mode    ; Far jump to 32-bit code

start_protected_mode:
    ; This is still 16-bit code. We use a trampoline here to switch to 32-bit.
    ; Set up a stack for 32-bit mode, etc.

    ; Actual jump to 32-bit code
    jmp 0x08:init_pm


CODE_SEG equ gdt_code - gdt_start   ; Offset for code segment

; GDT setup
gdt_start:
    dd 0                    ; Null descriptor
    dd 0

    gdt_code:               ; Code segment descriptor
    dw 0xFFFF
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0

    gdt_data:               ; Data segment descriptor
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .text
enable_a20:                 ; A20 line enable code here
    ; ... (A20 line enable code) ...
    ret

section .text32
bits 32
init_pm:
    ; Now we are in 32-bit code
    mov ax, gdt_data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax


    mov esp, stack_space    ; Set up the stack

    call main               ; Call the main function (C code)
    hlt                     ; Halt the CPU if main returns

section .bss
resb 8192                  ; 8KB for stack
stack_space:
