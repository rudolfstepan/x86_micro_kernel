; Freestanding ELF64 ET_EXEC fixture built by the normal x86_64 toolchain.
; R8.1e validates and stages this image but deliberately never executes it.

BITS 64

section .text
global _start
_start:
    push rdi
    pop rdi
    test edi, edi
    jnz probe_fault
    mov eax, 9
    mov edi, 100
    syscall
    ud2

probe_fault:
    ud2

section .data
align 8
probe_data:
    dq 0x3634464C45545349

section .bss
alignb 16
probe_zero_tail:
    resb 64
