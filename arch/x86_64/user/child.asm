BITS 64

REIST_SYS_EXIT equ 9
CHILD_STATUS   equ 77

section .text
global _start

_start:
    mov eax, REIST_SYS_EXIT
    mov edi, CHILD_STATUS
    xor esi, esi
    xor edx, edx
    syscall
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
