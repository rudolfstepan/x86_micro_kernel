; @file kernel/sched/user_entry.asm
; @brief Betritt Ring 3 mit einem vorbereiteten Userspace-Kontext.
;
; Layer: Ring-0/Ring-3 x86 privilege transition.
; Contract: Selektoren, Userstack, EFLAGS und Entry wurden zuvor validiert;
;           IRET stellt den vollständigen niedrigprivilegierten Kontext her.
; Safety: Unvalidierte Adressen oder Kernel-Selektoren dürfen diesen Pfad
;         nicht erreichen.

[BITS 32]
global enter_user_mode

section .text
enter_user_mode:
    cli
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push dword 0x23
    push edx
    pushfd
    or dword [esp], 0x200
    push dword 0x1B
    push eax
    iretd
