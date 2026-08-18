; @file arch/x86/cpu/idt.asm
; @brief IDT-Ladeprimitive.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: LIDT erhält ausschließlich einen vollständig initialisierten Deskriptor.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
[BITS 32]
global idt_load
extern idtp

section .text
idt_load:
    lidt [idtp]
    ret
