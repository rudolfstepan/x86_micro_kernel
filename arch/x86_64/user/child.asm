BITS 64

REIST_SYS_EXIT equ 9
REIST_SYS_GETPID equ 22
REIST_EACCES    equ -13
CHILD_STATUS   equ 77
FAIL_STATUS    equ 78
USER_STACK_TOP equ 0x00409000
CHILD_RSP      equ USER_STACK_TOP - 96
ARGV0_ADDRESS  equ USER_STACK_TOP - 32
ARGV1_ADDRESS  equ USER_STACK_TOP - 16

section .text
global _start

_start:
    mov eax, REIST_SYS_GETPID
    xor edi, edi
    xor esi, esi
    xor edx, edx
    syscall
    cmp rax, REIST_EACCES
    jne .fail
    cmp rsp, CHILD_RSP
    jne .fail
    test rsp, 15
    jnz .fail
    cmp qword [rsp], 2
    jne .fail
    cmp qword [rsp + 8], ARGV0_ADDRESS
    jne .fail
    cmp qword [rsp + 16], ARGV1_ADDRESS
    jne .fail
    cmp qword [rsp + 24], 0
    jne .fail
    cmp qword [rsp + 32], 0
    jne .fail
    cmp qword [rsp + 40], 0
    jne .fail
    cmp qword [rsp + 48], 0
    jne .fail
    mov rsi, qword [rsp + 8]
    mov rax, 0x632F6C6C6568732F
    cmp qword [rsi], rax
    jne .fail
    cmp dword [rsi + 8], 0x646C6968
    jne .fail
    cmp byte [rsi + 12], 0
    jne .fail
    mov rsi, qword [rsp + 16]
    mov rax, 0x0037376E656B6F74
    cmp qword [rsi], rax
    jne .fail
    mov edi, CHILD_STATUS
    jmp .exit
.fail:
    mov edi, FAIL_STATUS
.exit:
    mov eax, REIST_SYS_EXIT
    xor esi, esi
    xor edx, edx
    syscall
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
