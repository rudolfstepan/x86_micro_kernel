; @file arch/x86/boot/stack.asm
; @brief Früher Kernel-Bootstack.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Reserviert einen ausgerichteten, festen Stack vor Heap- und Paginginitialisierung.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
section .bss
resb 8192
stack_start:
