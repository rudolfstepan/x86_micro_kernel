; Freestanding ELF64 ET_EXEC fixture built by the normal x86_64 toolchain.
; R8.1e validates and stages this image. R8.1f executes modes 0 and 1;
; R8.1g executes the isolated cooperative task modes 0x0A and 0x0B.

BITS 64

section .text
global _start
_start:
    push rdi
    pop rdi
    test edi, edi
    jz probe_exit
    cmp edi, 1
    je probe_fault
    cmp edi, 0x0A
    je scheduler_task_a
    cmp edi, 0x0B
    je scheduler_task_b
    cmp edi, 0x0C
    je preempt_task_a
    cmp edi, 0x0D
    je preempt_task_b
    cmp edi, 0x0E
    je quantum_task_a
    cmp edi, 0x0F
    je quantum_task_b
    int3

probe_exit:
    mov eax, 9
    mov edi, 100
    syscall
    ud2

probe_fault:
    ud2

scheduler_task_a:
    mov rax, 0xA11A11A11A11A11A
    mov qword [rel probe_data], rax
    mov eax, 40
    syscall
    mov rax, 0xA11A11A11A11A11A
    cmp qword [rel probe_data], rax
    jne scheduler_isolation_failure
    mov eax, 40
    syscall
    mov rax, 0xA11A11A11A11A11A
    cmp qword [rel probe_data], rax
    jne scheduler_isolation_failure
    mov eax, 9
    mov edi, 101
    syscall
    ud2

scheduler_task_b:
    mov rax, 0xB22B22B22B22B22B
    mov qword [rel probe_data], rax
    mov eax, 40
    syscall
    mov rax, 0xB22B22B22B22B22B
    cmp qword [rel probe_data], rax
    jne scheduler_isolation_failure
scheduler_fault:
    ud2

scheduler_isolation_failure:
    int3

preempt_task_a:
    mov rax, 0xC33C33C33C33C33C
    mov qword [rel probe_data], rax
    mov eax, 40
    syscall
    mov rax, 0xC33C33C33C33C33C
    cmp qword [rel probe_data], rax
    jne scheduler_isolation_failure
    mov eax, 9
    mov edi, 102
    syscall
    ud2

preempt_task_b:
    mov rbx, rdx
.bounded_cpu_loop:
    pause
    rdtsc
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    cmp rax, rbx
    jb .bounded_cpu_loop
    int3

quantum_task_a:
    mov r14, 0xE44E44E44E44E44E
    mov r13, r14
    mov qword [rel probe_data], r14
    xor r15d, r15d
.quantum_a_loop:
    cmp r14, r13
    jne scheduler_isolation_failure
    inc qword [rel probe_progress]
    test r15, r15
    jnz .quantum_a_complete
    rdtsc
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    cmp rax, rbx
    jb .quantum_a_loop
    int3
.quantum_a_complete:
    mov rax, 0xE44E44E44E44E44E
    cmp qword [rel probe_data], rax
    jne scheduler_isolation_failure
    cmp qword [rel probe_progress], 1
    jbe scheduler_isolation_failure
    mov eax, 9
    mov edi, 103
    syscall
    ud2

quantum_task_b:
    mov r14, 0xF55F55F55F55F55F
    mov r13, r14
    mov qword [rel probe_data], r14
.quantum_b_loop:
    cmp r14, r13
    jne scheduler_isolation_failure
    inc qword [rel probe_progress]
    rdtsc
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    cmp rax, rbx
    jb .quantum_b_loop
    int3

section .data
align 8
probe_data:
    dq 0x3634464C45545349
    dq scheduler_fault
probe_progress:
    dq 0

section .bss
alignb 16
probe_zero_tail:
    resb 64
