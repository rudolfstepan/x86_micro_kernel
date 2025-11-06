[BITS 32]
global idt_load
extern idtp

section .text
idt_load:
    lidt [idtp]
    ret
