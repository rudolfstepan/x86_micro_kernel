; @file arch/x86/boot/ap_trampoline.asm
; @brief Position-independent real-mode bootstrap copied to physical 0x7000.
;
; APs deliberately keep interrupts disabled and never load the BSP TSS.  C
; validates their identity and parks them until a later per-CPU scheduler slice.

%define SMP_TRAMPOLINE_BASE 0x7000
%define SMP_MAILBOX_BASE    0x7200
%define MAILBOX_CPU_INDEX   8
%define MAILBOX_STACK_TOP   12
%define MAILBOX_ENTRY       16
%define MAILBOX_CR3         20
%define MAILBOX_GDTR        24
%define MAILBOX_IDTR        30

section .rodata align=16
global x86_ap_trampoline_start
global x86_ap_trampoline_end

[BITS 16]
x86_ap_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, SMP_TRAMPOLINE_BASE
    lgdt [SMP_MAILBOX_BASE + MAILBOX_GDTR]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:(SMP_TRAMPOLINE_BASE + .protected - x86_ap_trampoline_start)

[BITS 32]
.protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [SMP_MAILBOX_BASE + MAILBOX_CR3]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    lidt [SMP_MAILBOX_BASE + MAILBOX_IDTR]
    mov esp, [SMP_MAILBOX_BASE + MAILBOX_STACK_TOP]
    and esp, 0xfffffff0
    sub esp, 8
    push dword [SMP_MAILBOX_BASE + MAILBOX_CPU_INDEX]
    mov eax, [SMP_MAILBOX_BASE + MAILBOX_ENTRY]
    call eax
    cli
.halt:
    hlt
    jmp .halt
x86_ap_trampoline_end:
