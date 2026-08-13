[BITS 32]

extern exception_dispatcher

%macro ISR_NO_ERROR_CODE 1
global isr%1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_CPU_ERROR_CODE 1
global isr%1
isr%1:
    cli
    ; The processor already pushed the exception-specific error code.
    push dword %1
    jmp isr_common_stub
%endmacro

section .text
global page_fault_handler_asm
page_fault_handler_asm:
    jmp isr14              ; Compatibility alias; isr14 builds a full Registers frame

; Keep this list aligned with the IA-32 exception table.  Vectors 21, 29 and
; 30 are feature/vendor extensions but use the same error-code frame when the
; corresponding exception is available.
ISR_NO_ERROR_CODE 0
ISR_NO_ERROR_CODE 1
ISR_NO_ERROR_CODE 2
ISR_NO_ERROR_CODE 3
ISR_NO_ERROR_CODE 4
ISR_NO_ERROR_CODE 5
ISR_NO_ERROR_CODE 6
ISR_NO_ERROR_CODE 7
ISR_CPU_ERROR_CODE 8
ISR_NO_ERROR_CODE 9
ISR_CPU_ERROR_CODE 10
ISR_CPU_ERROR_CODE 11
ISR_CPU_ERROR_CODE 12
ISR_CPU_ERROR_CODE 13
ISR_CPU_ERROR_CODE 14
ISR_NO_ERROR_CODE 15
ISR_NO_ERROR_CODE 16
ISR_CPU_ERROR_CODE 17
ISR_NO_ERROR_CODE 18
ISR_NO_ERROR_CODE 19
ISR_NO_ERROR_CODE 20
ISR_CPU_ERROR_CODE 21
ISR_NO_ERROR_CODE 22
ISR_NO_ERROR_CODE 23
ISR_NO_ERROR_CODE 24
ISR_NO_ERROR_CODE 25
ISR_NO_ERROR_CODE 26
ISR_NO_ERROR_CODE 27
ISR_NO_ERROR_CODE 28
ISR_CPU_ERROR_CODE 29
ISR_CPU_ERROR_CODE 30
ISR_NO_ERROR_CODE 31

isr_common_stub:
    pusha                      ; Save general-purpose registers
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10               ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    mov ebx, esp               ; Preserve the Registers frame across the C call
    and esp, 0xfffffff0
    sub esp, 12
    push ebx                   ; Pass pointer with ABI-compliant call alignment

    call exception_dispatcher

    mov esp, ebx               ; Restore the exact interrupt frame
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                 ; Remove vector and real/synthetic error code
    iret
