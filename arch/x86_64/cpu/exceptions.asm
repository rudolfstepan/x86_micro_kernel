; Bounded exception-entry foundation for the isolated REIST x86_64 proof.
; Hardware interrupts remain masked; this unit publishes only vectors 0..31.

BITS 64

EXCEPTION_VECTOR_COUNT equ 32
IDT_GATE_SIZE         equ 16
IDT_GATE_PRESENT_INT  equ 0x8E
DOUBLE_FAULT_VECTOR   equ 8
TSS64_SIZE            equ 104
TSS_IOPB_OFFSET       equ 102
TSS_IST1              equ 1
TSS_SELECTOR          equ 0x18
KERNEL_CODE_SELECTOR  equ 0x08
KERNEL_DATA_SELECTOR  equ 0x10
EXCEPTION_FRAME_VECTOR equ (15 * 8)
EXCEPTION_FRAME_ERROR  equ (EXCEPTION_FRAME_VECTOR + 8)
EXCEPTION_FRAME_RIP    equ (EXCEPTION_FRAME_ERROR + 8)

section .text
global x86_64_exception_init
extern serial_init64
extern serial_write64
extern serial_putc64
extern halt64
extern x86_64_ud2_probe
extern x86_64_ud2_resume

x86_64_exception_init:
    cld

    ; Multiboot does not guarantee BSS contents. Initialize the complete TSS
    ; before publishing its descriptor or loading TR.
    xor eax, eax
    lea rdi, [rel exception_tss]
    mov ecx, TSS64_SIZE / 8
    rep stosq

    lea rax, [rel double_fault_ist_top]
    mov qword [rel exception_tss + 36], rax
    mov word [rel exception_tss + TSS_IOPB_OFFSET], TSS64_SIZE

    mov qword [rel exception_gdt_tss], 0
    mov qword [rel exception_gdt_tss + 8], 0
    lea rax, [rel exception_tss]
    mov word [rel exception_gdt_tss], TSS64_SIZE - 1
    mov word [rel exception_gdt_tss + 2], ax
    shr rax, 16
    mov byte [rel exception_gdt_tss + 4], al
    mov byte [rel exception_gdt_tss + 5], 0x89
    mov byte [rel exception_gdt_tss + 6], 0
    mov byte [rel exception_gdt_tss + 7], ah
    shr rax, 16
    mov dword [rel exception_gdt_tss + 8], eax

    lea rax, [rel exception_gdt]
    mov qword [rel exception_gdt_pointer + 2], rax
    lgdt [rel exception_gdt_pointer]
    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov ax, TSS_SELECTOR
    ltr ax

    ; Build every gate completely before LIDT. All handler addresses are in
    ; the existing low 2-MiB identity map, so their upper 32 bits are zero.
    lea rsi, [rel exception_stub_table]
    lea rdi, [rel exception_idt]
    xor ecx, ecx
.gate_loop:
    mov eax, dword [rsi + rcx * 4]
    mov word [rdi], ax
    mov word [rdi + 2], KERNEL_CODE_SELECTOR
    mov byte [rdi + 4], 0
    cmp ecx, DOUBLE_FAULT_VECTOR
    jne .gate_no_ist
    mov byte [rdi + 4], TSS_IST1
.gate_no_ist:
    mov byte [rdi + 5], IDT_GATE_PRESENT_INT
    shr eax, 16
    mov word [rdi + 6], ax
    mov dword [rdi + 8], 0
    mov dword [rdi + 12], 0
    add rdi, IDT_GATE_SIZE
    inc ecx
    cmp ecx, EXCEPTION_VECTOR_COUNT
    jb .gate_loop

    lea rax, [rel exception_idt]
    mov qword [rel exception_idt_pointer + 2], rax
    lidt [rel exception_idt_pointer]
    mov esi, exception_idt_ready_message
    call serial_write64
    ret

%macro EXCEPTION_NOERR 1
exception_stub_%1:
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro EXCEPTION_ERR 1
exception_stub_%1:
    push qword %1
    jmp exception_common
%endmacro

EXCEPTION_NOERR 0
EXCEPTION_NOERR 1
EXCEPTION_NOERR 2
EXCEPTION_NOERR 3
EXCEPTION_NOERR 4
EXCEPTION_NOERR 5
EXCEPTION_NOERR 6
EXCEPTION_NOERR 7
EXCEPTION_ERR 8
EXCEPTION_NOERR 9
EXCEPTION_ERR 10
EXCEPTION_ERR 11
EXCEPTION_ERR 12
EXCEPTION_ERR 13
EXCEPTION_ERR 14
EXCEPTION_NOERR 15
EXCEPTION_NOERR 16
EXCEPTION_ERR 17
EXCEPTION_NOERR 18
EXCEPTION_NOERR 19
EXCEPTION_NOERR 20
EXCEPTION_ERR 21
EXCEPTION_NOERR 22
EXCEPTION_NOERR 23
EXCEPTION_NOERR 24
EXCEPTION_NOERR 25
EXCEPTION_NOERR 26
EXCEPTION_NOERR 27
EXCEPTION_NOERR 28
EXCEPTION_ERR 29
EXCEPTION_ERR 30
EXCEPTION_NOERR 31

exception_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cmp qword [rsp + EXCEPTION_FRAME_VECTOR], 6
    jne exception_fatal
    cmp qword [rsp + EXCEPTION_FRAME_ERROR], 0
    jne exception_fatal
    mov rax, [rsp + EXCEPTION_FRAME_RIP]
    mov edx, x86_64_ud2_probe
    cmp rax, rdx
    jne exception_fatal
    mov edx, x86_64_ud2_resume
    mov [rsp + EXCEPTION_FRAME_RIP], rdx
    mov esi, exception_ud_ok_message
    call serial_write64

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

exception_fatal:
    call serial_init64
    mov esi, exception_fatal_message
    call serial_write64
    mov rax, [rsp + EXCEPTION_FRAME_VECTOR]
    call serial_hex8
    mov esi, newline_message
    call serial_write64
    jmp halt64

serial_hex8:
    mov r8b, al
    shr al, 4
    call serial_hex_nibble
    mov al, r8b
    and al, 0x0F
serial_hex_nibble:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp serial_putc64
.digit:
    add al, '0'
    jmp serial_putc64

section .rodata
exception_idt_ready_message db "REIST_X86_64_EXCEPTION_IDT_READY", 13, 10, 0
exception_ud_ok_message db "REIST_X86_64_EXCEPTION_UD_OK", 13, 10, 0
exception_fatal_message db "REIST_X86_64_EXCEPTION_FATAL vector=", 0
newline_message db 13, 10, 0

align 8
exception_stub_table:
%assign vector 0
%rep EXCEPTION_VECTOR_COUNT
    dd exception_stub_%+vector
%assign vector vector + 1
%endrep

align 8
exception_gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
exception_gdt_tss:
    dq 0
    dq 0
exception_gdt_end:

exception_gdt_pointer:
    dw exception_gdt_end - exception_gdt - 1
    dq 0

exception_idt_pointer:
    dw (EXCEPTION_VECTOR_COUNT * IDT_GATE_SIZE) - 1
    dq 0

section .bss
alignb 16
exception_tss:
    resb TSS64_SIZE
alignb 16
exception_idt:
    resb EXCEPTION_VECTOR_COUNT * IDT_GATE_SIZE
alignb 16
double_fault_ist_bottom:
    resb 16384
double_fault_ist_top:
