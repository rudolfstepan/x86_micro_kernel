; @file arch/x86/cpu/gdt.asm
; @brief GDT-/TSS-Ladeprimitive.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Segmentregister und Taskregister werden in definierter Reihenfolge aktualisiert.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
[BITS 32]
global gdt_flush
global tss_flush
extern gp

section .text
gdt_flush:
    lgdt [gp]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:flush2
flush2:
    ret

; Load TSS into Task Register
; void tss_flush(uint16_t selector);
tss_flush:
    mov ax, [esp + 4]   ; Get selector argument from stack
    ltr ax              ; Load Task Register with TSS selector
    ret
