BITS 64

REIST_SYS_EXIT equ 9
REIST_SYS_GETPID equ 22
REIST_SYS_YIELD equ 40
REIST_SYS_IPC_SEND equ 50
REIST_SYS_IPC_RECEIVE equ 51
REIST_SYS_IPC_RELEASE equ 58
REIST_EACCES    equ -13
REIST_EAGAIN    equ -11
CHILD_STATUS   equ 77
FAIL_STATUS    equ 78
USER_STACK_TOP equ 0x00409000
CHILD_RSP      equ USER_STACK_TOP - 128
ARGV0_ADDRESS  equ USER_STACK_TOP - 32
ARGV1_ADDRESS  equ USER_STACK_TOP - 16
AT_REIST_IPC_HANDLE equ 0x52534901
IPC_MESSAGE_VERSION equ 1
IPC_MESSAGE_SIZE equ 140
IPC_MESSAGE_LENGTH equ 8
IPC_STACK_BYTES equ 144

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
    cmp qword [rsp + 40], AT_REIST_IPC_HANDLE
    jne .fail
    mov r12, qword [rsp + 48]
    test r12, r12
    jz .fail
    cmp qword [rsp + 56], 0
    jne .fail
    cmp qword [rsp + 64], 0
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
    sub rsp, IPC_STACK_BYTES
    cld
    mov rdi, rsp
    xor eax, eax
    mov ecx, IPC_STACK_BYTES / 8
    rep stosq
    mov dword [rsp], IPC_MESSAGE_VERSION
    mov dword [rsp + 4], IPC_MESSAGE_SIZE
    mov dword [rsp + 8], IPC_MESSAGE_LENGTH
    mov rax, 0x0036376E656B6F74
    mov qword [rsp + 12], rax
    mov eax, REIST_SYS_IPC_RECEIVE
    mov rdi, r12
    mov rsi, rsp
    xor edx, edx
    syscall
    cmp rax, REIST_EACCES
    jne .fail
    mov eax, REIST_SYS_IPC_SEND
    mov rdi, r12
    mov rsi, rsp
    xor edx, edx
    syscall
    test rax, rax
    jnz .fail
    mov byte [rsp + 18], '7'
    mov eax, REIST_SYS_IPC_SEND
    mov rdi, r12
    mov rsi, rsp
    xor edx, edx
    syscall
    cmp rax, REIST_EAGAIN
    jne .fail
    mov eax, REIST_SYS_YIELD
    xor edi, edi
    xor esi, esi
    xor edx, edx
    syscall
    test rax, rax
    jnz .fail
    mov eax, REIST_SYS_IPC_SEND
    mov rdi, r12
    mov rsi, rsp
    xor edx, edx
    syscall
    test rax, rax
    jnz .fail
    mov eax, REIST_SYS_IPC_RELEASE
    mov rdi, r12
    xor esi, esi
    xor edx, edx
    syscall
    test rax, rax
    jnz .fail
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
