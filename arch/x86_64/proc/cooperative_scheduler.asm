; Fixed two-process cooperative scheduler proof for the isolated x86_64 image.
; It is deliberately not linked into any production i386 kernel or image.

BITS 64

TASK_COUNT                 equ 2
TASK_RECORD_SIZE           equ 256
TASK_STATE                 equ 0
TASK_GENERATION            equ 8
TASK_CR3                   equ 16
TASK_STACK_FRAME           equ 24
TASK_PRIVATE_FRAMES        equ 32
TASK_RIP                   equ 96
TASK_RSP                   equ 104
TASK_RFLAGS                equ 112
TASK_RBX                   equ 120
TASK_RDX                   equ 128
TASK_RBP                   equ 136
TASK_RSI                   equ 144
TASK_RDI                   equ 152
TASK_R8                    equ 160
TASK_R9                    equ 168
TASK_R10                   equ 176
TASK_R12                   equ 184
TASK_R13                   equ 192
TASK_R14                   equ 200
TASK_R15                   equ 208
TASK_YIELDS                equ 216
TASK_ID                    equ 224
TASK_RAX                   equ 232
TASK_RCX                   equ 240
TASK_R11                   equ 248

TASK_FREE                  equ 0
TASK_READY                 equ 1
TASK_RUNNING               equ 2
TASK_FAULTED               equ 3
TASK_EXITED                equ 4
TASK_PREEMPTED             equ 5
TASK_A_GENERATION          equ 1
TASK_B_GENERATION          equ 2
TASK_A_ID                  equ 0x0A
TASK_B_ID                  equ 0x0B
TASK_A_PREEMPT_ID          equ 0x0C
TASK_B_PREEMPT_ID          equ 0x0D
TASK_A_QUANTUM_ID          equ 0x0E
TASK_B_QUANTUM_ID          equ 0x0F
SCHEDULER_MODE_COOPERATIVE equ 1
SCHEDULER_MODE_PREEMPTION  equ 2
SCHEDULER_MODE_QUANTUM     equ 3

EVENT_A_READY              equ 1
EVENT_B_READY              equ 2
EVENT_A_RUNNING            equ 3
EVENT_A_READY_AGAIN        equ 4
EVENT_B_RUNNING            equ 5
EVENT_B_READY_AGAIN        equ 6
EVENT_B_FAULTED            equ 7
EVENT_B_FREE               equ 8
EVENT_A_EXITED             equ 9
EVENT_A_FREE               equ 10
EVENT_B_PREEMPTED          equ 11
EVENT_CAPACITY             equ 16

USER_BASE                  equ 0x00400000
USER_PAGE_COUNT            equ 8
USER_STACK_BASE            equ 0x00408000
USER_STACK_TOP             equ 0x00409000
PROBE_DATA_PAGE_INDEX      equ 1
PROBE_FAULT_POINTER_OFFSET equ 8
PROBE_PROGRESS_OFFSET      equ 16
QUANTUM_A_MAGIC            equ 0xE44E44E44E44E44E
QUANTUM_B_MAGIC            equ 0xF55F55F55F55F55F
PAGE_SIZE                  equ 4096
MANAGED_LIMIT              equ 0x04000000
DIRECT_MAP_BASE            equ 0xFFFF800000000000

PAGE_PRESENT               equ 0x001
PAGE_WRITE                 equ 0x002
PAGE_USER                  equ 0x004
PAGE_NX                    equ 0x8000000000000000
PF_X                       equ 1
PF_W                       equ 2
PF_R                       equ 4

KERNEL_DATA_SELECTOR       equ 0x10
USER_DATA_SELECTOR         equ 0x2B
USER_CODE_SELECTOR         equ 0x33
USER_RFLAGS_INITIAL        equ 0x0000000000000002
RFLAGS_FIXED_BIT           equ 0x0000000000000002
RFLAGS_RF                  equ 0x0000000000010000
RFLAGS_SYSCALL_FORBIDDEN   equ 0x00000000003F7F00
RFLAGS_PREEMPT_FORBIDDEN   equ 0x00000000003F7D00
RFLAGS_IRQ_FORBIDDEN       equ 0x00000000003E7500
RFLAGS_FAULT_FORBIDDEN     equ 0x00000000003E7F00

REIST_SYS_EXIT             equ 9
REIST_SYS_YIELD            equ 40
EXPECTED_EXIT_STATUS       equ 101
PREEMPT_EXIT_STATUS        equ 102
QUANTUM_EXIT_STATUS        equ 103

IA32_EFER                  equ 0xC0000080
IA32_STAR                  equ 0xC0000081
IA32_LSTAR                 equ 0xC0000082
IA32_FMASK                 equ 0xC0000084
IA32_GS_BASE               equ 0xC0000101
IA32_KERNEL_GS_BASE        equ 0xC0000102
EFER_SCE                   equ 0x001
STAR_VALUE                 equ 0x0020000800000000
SYSCALL_FMASK              equ 0x00000000003F4700

SYSCALL_CONTEXT_KERNEL_RSP equ 0
SYSCALL_CONTEXT_USER_RSP   equ 8

EXCEPTION_FRAME_VECTOR     equ (15 * 8)
EXCEPTION_FRAME_R15        equ 0
EXCEPTION_FRAME_R14        equ 8
EXCEPTION_FRAME_R13        equ 16
EXCEPTION_FRAME_R12        equ 24
EXCEPTION_FRAME_R11        equ 32
EXCEPTION_FRAME_R10        equ 40
EXCEPTION_FRAME_R9         equ 48
EXCEPTION_FRAME_R8         equ 56
EXCEPTION_FRAME_RDI        equ 64
EXCEPTION_FRAME_RSI        equ 72
EXCEPTION_FRAME_RBP        equ 80
EXCEPTION_FRAME_RDX        equ 88
EXCEPTION_FRAME_RCX        equ 96
EXCEPTION_FRAME_RBX        equ 104
EXCEPTION_FRAME_RAX        equ 112
EXCEPTION_FRAME_ERROR      equ (EXCEPTION_FRAME_VECTOR + 8)
EXCEPTION_FRAME_RIP        equ (EXCEPTION_FRAME_ERROR + 8)
EXCEPTION_FRAME_CS         equ (EXCEPTION_FRAME_RIP + 8)
EXCEPTION_FRAME_RFLAGS     equ (EXCEPTION_FRAME_CS + 8)
EXCEPTION_FRAME_RSP        equ (EXCEPTION_FRAME_RFLAGS + 8)
EXCEPTION_FRAME_SS         equ (EXCEPTION_FRAME_RSP + 8)

section .text
global x86_64_process_scheduler_selftest64
global x86_64_scheduler_user_exception64
global x86_64_process_preemption_selftest64
global x86_64_scheduler_timer_preempt64
global x86_64_scheduler_timer_validate64
global x86_64_process_quantum_selftest64
global x86_64_scheduler_quantum_switch64
global x86_64_scheduler_quantum_validate64
global x86_64_scheduler_timer_abort64

extern pml4_table
extern physical_frame_alloc64
extern physical_frame_free64
extern physical_free_frame_count64
extern x86_64_elf64_load64
extern x86_64_elf64_release64
extern x86_64_elf64_entry64
extern x86_64_elf64_page_frame64
extern x86_64_elf64_page_flags64
extern x86_64_elf64_address_flags64
extern x86_64_exception_set_rsp0
extern serial_write64
extern serial_putc64
extern x86_64_timer_preemption_arm64
extern x86_64_timer_preemption_cancel64
extern x86_64_timer_preemption_disarm64
extern x86_64_timer_quantum_arm64
extern x86_64_timer_quantum_disarm64

x86_64_process_scheduler_selftest64:
    cli
    cld
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne scheduler_fail
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax

    xor eax, eax
    lea rdi, [rel scheduler_state_begin]
    mov ecx, (scheduler_state_end - scheduler_state_begin) / 8
    rep stosq
    mov byte [rel scheduler_failure_stage], 1
    mov byte [rel scheduler_mode], SCHEDULER_MODE_COOPERATIVE
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax

    mov byte [rel scheduler_failure_stage], 2
    call x86_64_elf64_load64
    test eax, eax
    jz scheduler_fail
    call x86_64_elf64_entry64
    cmp rax, USER_BASE
    jb scheduler_fail
    cmp rax, USER_STACK_BASE
    jae scheduler_fail
    mov qword [rel scheduler_entry], rax
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz scheduler_fail

    mov byte [rel scheduler_failure_stage], 3
    xor edi, edi
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    mov edi, 1
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_isolation64
    test eax, eax
    jz scheduler_fail

    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    mov qword [r12 + TASK_ID], TASK_A_ID
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_A_READY
    call scheduler_append_event64
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    mov qword [r12 + TASK_ID], TASK_B_ID
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_B_READY
    call scheduler_append_event64

    mov byte [rel scheduler_failure_stage], 4
    call scheduler_setup_syscalls64
    test eax, eax
    jz scheduler_fail
    lea rax, [rel scheduler_kernel_stack_top]
    mov qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz scheduler_fail

    mov byte [rel scheduler_active], 1
    mov byte [rel scheduler_failure_stage], 5
    xor edi, edi
    jmp scheduler_enter_task64

x86_64_process_preemption_selftest64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0xA0
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne scheduler_fail
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax
    xor eax, eax
    lea rdi, [rel scheduler_state_begin]
    mov ecx, (scheduler_state_end - scheduler_state_begin) / 8
    rep stosq
    mov byte [rel scheduler_failure_stage], 0xA1
    mov byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax
    mov byte [rel scheduler_failure_stage], 0xA2
    call x86_64_elf64_load64
    test eax, eax
    jz scheduler_fail
    call x86_64_elf64_entry64
    mov qword [rel scheduler_entry], rax
    mov byte [rel scheduler_failure_stage], 0xA3
    xor edi, edi
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    mov edi, 1
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_isolation64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0xA4
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_A_READY
    call scheduler_append_event64
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_B_READY
    call scheduler_append_event64
    mov byte [rel scheduler_failure_stage], 0xA5
    call scheduler_setup_syscalls64
    test eax, eax
    jz scheduler_fail
    lea rax, [rel scheduler_kernel_stack_top]
    mov qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_active], 1
    mov byte [rel scheduler_failure_stage], 0xA6
    xor edi, edi
    jmp scheduler_enter_task64

x86_64_process_quantum_selftest64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0xE0
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne scheduler_fail
    xor eax, eax
    lea rdi, [rel scheduler_state_begin]
    mov ecx, (scheduler_state_end - scheduler_state_begin) / 8
    rep stosq
    mov byte [rel scheduler_failure_stage], 0xE1
    mov byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax
    call x86_64_elf64_load64
    test eax, eax
    jz scheduler_fail
    call x86_64_elf64_entry64
    mov qword [rel scheduler_entry], rax
    mov byte [rel scheduler_failure_stage], 0xE2
    xor edi, edi
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    mov edi, 1
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_isolation64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_A_READY
    call scheduler_append_event64
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_B_READY
    call scheduler_append_event64
    mov byte [rel scheduler_failure_stage], 0xE3
    call scheduler_setup_syscalls64
    test eax, eax
    jz scheduler_fail
    lea rax, [rel scheduler_kernel_stack_top]
    mov qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz scheduler_fail
    call x86_64_timer_quantum_arm64
    test rax, rax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_RBX], rax
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r12 + TASK_RBX], rax
    mov byte [rel scheduler_active], 1
    mov byte [rel scheduler_failure_stage], 0xE4
    xor edi, edi
    jmp scheduler_enter_task64

; EDI is the only admitted slot. The scan bound remains exactly two slots;
; callers derive EDI from the finite expected handoff sequence.
scheduler_enter_task64:
    cmp byte [rel scheduler_active], 1
    jne scheduler_fail
    cmp edi, TASK_COUNT
    jae scheduler_fail
    mov dword [rel scheduler_current_slot], edi
    mov eax, edi
    shl rax, 8
    lea r11, [rel scheduler_tasks]
    add r11, rax
    cmp qword [r11 + TASK_STATE], TASK_READY
    jne scheduler_fail
    test edi, edi
    jnz .task_b
    cmp qword [r11 + TASK_GENERATION], TASK_A_GENERATION
    jne scheduler_fail
    mov al, EVENT_A_RUNNING
    jmp .state_ready
.task_b:
    cmp qword [r11 + TASK_GENERATION], TASK_B_GENERATION
    jne scheduler_fail
    mov al, EVENT_B_RUNNING
.state_ready:
    mov qword [r11 + TASK_STATE], TASK_RUNNING
    call scheduler_append_event64

    mov rax, qword [r11 + TASK_CR3]
    test rax, PAGE_SIZE - 1
    jnz scheduler_fail
    mov cr3, rax

    push qword USER_DATA_SELECTOR
    push qword [r11 + TASK_RSP]
    push qword [r11 + TASK_RFLAGS]
    push qword USER_CODE_SELECTOR
    push qword [r11 + TASK_RIP]

    mov ax, USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_registers
    mov rbx, qword [r11 + TASK_RBX]
    mov rdx, qword [r11 + TASK_RDX]
    mov rbp, qword [r11 + TASK_RBP]
    mov rsi, qword [r11 + TASK_RSI]
    mov rdi, qword [r11 + TASK_RDI]
    mov r8, qword [r11 + TASK_R8]
    mov r9, qword [r11 + TASK_R9]
    mov r10, qword [r11 + TASK_R10]
    mov r12, qword [r11 + TASK_R12]
    mov r13, qword [r11 + TASK_R13]
    mov r14, qword [r11 + TASK_R14]
    mov r15, qword [r11 + TASK_R15]
    xor eax, eax
    xor ecx, ecx
    xor r11d, r11d
    iretq
.quantum_registers:
    mov rax, qword [r11 + TASK_RAX]
    mov rbx, qword [r11 + TASK_RBX]
    mov rcx, qword [r11 + TASK_RCX]
    mov rdx, qword [r11 + TASK_RDX]
    mov rbp, qword [r11 + TASK_RBP]
    mov rsi, qword [r11 + TASK_RSI]
    mov rdi, qword [r11 + TASK_RDI]
    mov r8, qword [r11 + TASK_R8]
    mov r9, qword [r11 + TASK_R9]
    mov r10, qword [r11 + TASK_R10]
    mov r12, qword [r11 + TASK_R12]
    mov r13, qword [r11 + TASK_R13]
    mov r14, qword [r11 + TASK_R14]
    mov r15, qword [r11 + TASK_R15]
    mov r11, qword [r11 + TASK_R11]
    iretq

; No user register is consumed before the architectural stack switch.
scheduler_syscall_entry64:
    swapgs
    mov qword [gs:SYSCALL_CONTEXT_USER_RSP], rsp
    mov rsp, qword [gs:SYSCALL_CONTEXT_KERNEL_RSP]
    swapgs
    cld

    mov qword [rel syscall_rax], rax
    mov qword [rel syscall_rbx], rbx
    mov qword [rel syscall_rcx], rcx
    mov qword [rel syscall_rdx], rdx
    mov qword [rel syscall_rbp], rbp
    mov qword [rel syscall_rsi], rsi
    mov qword [rel syscall_rdi], rdi
    mov qword [rel syscall_r8], r8
    mov qword [rel syscall_r9], r9
    mov qword [rel syscall_r10], r10
    mov qword [rel syscall_r11], r11
    mov qword [rel syscall_r12], r12
    mov qword [rel syscall_r13], r13
    mov qword [rel syscall_r14], r14
    mov qword [rel syscall_r15], r15

    cmp byte [rel scheduler_active], 1
    jne scheduler_fail
    mov edi, dword [rel scheduler_current_slot]
    cmp edi, TASK_COUNT
    jae scheduler_fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne scheduler_fail
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne scheduler_fail
    cmp qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_USER_RSP], USER_STACK_TOP
    jne scheduler_fail
    mov rax, qword [rel syscall_r11]
    test rax, RFLAGS_FIXED_BIT
    jz scheduler_fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preempt_syscall_flags
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .preempt_syscall_flags
    test rax, RFLAGS_SYSCALL_FORBIDDEN
    jnz scheduler_fail
    jmp .syscall_flags_valid
.preempt_syscall_flags:
    test rax, RFLAGS_PREEMPT_FORBIDDEN
    jnz scheduler_fail
    test rax, 0x200
    jz scheduler_fail
.syscall_flags_valid:
    mov rax, qword [rel syscall_rcx]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz scheduler_fail

    cmp qword [rel syscall_rax], REIST_SYS_YIELD
    je scheduler_handle_yield64
    cmp qword [rel syscall_rax], REIST_SYS_EXIT
    je scheduler_handle_exit64
    jmp scheduler_fail

scheduler_handle_yield64:
    mov rax, qword [rel syscall_rcx]
    mov qword [r12 + TASK_RIP], rax
    mov rax, qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_USER_RSP]
    mov qword [r12 + TASK_RSP], rax
    mov rax, qword [rel syscall_r11]
    mov qword [r12 + TASK_RFLAGS], rax
    mov rax, qword [rel syscall_rbx]
    mov qword [r12 + TASK_RBX], rax
    mov rax, qword [rel syscall_rdx]
    mov qword [r12 + TASK_RDX], rax
    mov rax, qword [rel syscall_rbp]
    mov qword [r12 + TASK_RBP], rax
    mov rax, qword [rel syscall_rsi]
    mov qword [r12 + TASK_RSI], rax
    mov rax, qword [rel syscall_rdi]
    mov qword [r12 + TASK_RDI], rax
    mov rax, qword [rel syscall_r8]
    mov qword [r12 + TASK_R8], rax
    mov rax, qword [rel syscall_r9]
    mov qword [r12 + TASK_R9], rax
    mov rax, qword [rel syscall_r10]
    mov qword [r12 + TASK_R10], rax
    mov rax, qword [rel syscall_r12]
    mov qword [r12 + TASK_R12], rax
    mov rax, qword [rel syscall_r13]
    mov qword [r12 + TASK_R13], rax
    mov rax, qword [rel syscall_r14]
    mov qword [r12 + TASK_R14], rax
    mov rax, qword [rel syscall_r15]
    mov qword [r12 + TASK_R15], rax

    inc qword [r12 + TASK_YIELDS]
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preemption_yield
    cmp edi, 0
    jne .yield_b
    cmp qword [r12 + TASK_YIELDS], 2
    ja scheduler_fail
    mov al, EVENT_A_READY_AGAIN
    jmp .yield_ready
.yield_b:
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    mov al, EVENT_B_READY_AGAIN
.yield_ready:
    mov qword [r12 + TASK_STATE], TASK_READY
    call scheduler_append_event64
    inc dword [rel scheduler_handoffs]
    mov eax, dword [rel scheduler_handoffs]
    cmp eax, 1
    je .expect_a_to_b
    cmp eax, 2
    je .expect_b_to_a
    cmp eax, 3
    je .expect_a_to_b
    jmp scheduler_fail
.expect_a_to_b:
    cmp edi, 0
    jne scheduler_fail
    mov edi, 1
    jmp scheduler_enter_task64
.preemption_yield:
    mov byte [rel scheduler_failure_stage], 0xB1
    cmp edi, 0
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_A_READY_AGAIN
    call scheduler_append_event64
    inc dword [rel scheduler_handoffs]
    cmp dword [rel scheduler_handoffs], 1
    jne scheduler_fail
    call x86_64_timer_preemption_arm64
    test rax, rax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0xB2
    lea r13, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r13 + TASK_RDX], rax
    mov edi, 1
    jmp scheduler_enter_task64
.expect_b_to_a:
    cmp edi, 1
    jne scheduler_fail
    xor edi, edi
    jmp scheduler_enter_task64

scheduler_handle_exit64:
    cmp edi, 0
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    jne scheduler_fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preemption_exit
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_exit
    cmp qword [rel syscall_rdi], EXPECTED_EXIT_STATUS
    jne scheduler_fail
    cmp dword [rel scheduler_handoffs], 3
    jne scheduler_fail
    cmp dword [rel scheduler_fault_count], 1
    jne scheduler_fail
    cmp dword [rel scheduler_reap_count], 1
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 2
    jne scheduler_fail
    cmp qword [rel scheduler_b_yields], 1
    jne scheduler_fail
    jmp .exit_valid
.preemption_exit:
    mov byte [rel scheduler_failure_stage], 0xD1
    cmp qword [rel syscall_rdi], PREEMPT_EXIT_STATUS
    jne scheduler_fail
    cmp dword [rel scheduler_handoffs], 1
    jne scheduler_fail
    cmp dword [rel scheduler_fault_count], 1
    jne scheduler_fail
    cmp dword [rel scheduler_reap_count], 1
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    jmp .exit_valid
.quantum_exit:
    mov byte [rel scheduler_failure_stage], 0xF1
    cmp qword [rel syscall_rdi], QUANTUM_EXIT_STATUS
    jne scheduler_fail
    cmp dword [rel scheduler_handoffs], 4
    jne scheduler_fail
    cmp dword [rel scheduler_fault_count], 1
    jne scheduler_fail
    cmp dword [rel scheduler_reap_count], 1
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 0
    jne scheduler_fail
.exit_valid:
    mov qword [r12 + TASK_STATE], TASK_EXITED
    mov al, EVENT_A_EXITED
    call scheduler_append_event64

    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov byte [rel scheduler_active], 0
    mov edi, 0
    mov esi, TASK_A_GENERATION
    mov edx, TASK_EXITED
    mov ecx, EVENT_A_FREE
    mov byte [rel scheduler_failure_stage], 0xD2
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0xD3
    call scheduler_verify_final_events64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0xD4
    call scheduler_cleanup_common64
    test eax, eax
    jz scheduler_return_failure64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preemption_ok
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_ok
    lea rsi, [rel scheduler_ok_message]
    jmp .write_ok
.preemption_ok:
    lea rsi, [rel scheduler_preempt_ok_message]
    jmp .write_ok
.quantum_ok:
    lea rsi, [rel scheduler_quantum_ok_message]
.write_ok:
    call serial_write64
    mov byte [rel scheduler_final_result], 1
    jmp scheduler_return64

; RDI points at the normalized exception frame. Only B's exact terminal UD2
; after the third handoff is locally contained; every other frame returns 0.
x86_64_scheduler_user_exception64:
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_COOPERATIVE
    je .cooperative_exception
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je x86_64_scheduler_timer_abort64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je x86_64_scheduler_timer_abort64
    jmp .invalid
.cooperative_exception:
    cmp dword [rel scheduler_current_slot], 1
    jne .invalid
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid
    cmp qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    jne .invalid
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 6
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_CS], USER_CODE_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_SS], USER_DATA_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_RSP], USER_STACK_TOP
    jne .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    test rax, RFLAGS_FIXED_BIT
    jz .invalid
    test rax, RFLAGS_RF
    jz .invalid
    test rax, RFLAGS_FAULT_FORBIDDEN
    jnz .invalid
    cmp dword [rel scheduler_handoffs], 3
    jne .invalid
    cmp qword [r12 + TASK_YIELDS], 1
    jne .invalid
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .invalid
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    mov rax, qword [rax + PROBE_FAULT_POINTER_OFFSET]
    cmp rax, qword [rdi + EXCEPTION_FRAME_RIP]
    jne .invalid
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid

    mov qword [r12 + TASK_STATE], TASK_FAULTED
    mov al, EVENT_B_FAULTED
    call scheduler_append_event64
    inc dword [rel scheduler_fault_count]
    mov rax, qword [r12 + TASK_YIELDS]
    mov qword [rel scheduler_b_yields], rax
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    lea rsp, [rel scheduler_kernel_stack_top]
    call scheduler_restore_kernel_segments64
    mov edi, 1
    mov esi, TASK_B_GENERATION
    mov edx, TASK_FAULTED
    mov ecx, EVENT_B_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    xor edi, edi
    jmp scheduler_enter_task64
.invalid:
    xor eax, eax
    ret

x86_64_scheduler_timer_abort64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .abort
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    jne .invalid
.abort:
    mov rax, qword [rel scheduler_original_cr3]
    test rax, rax
    jz .invalid
    mov cr3, rax
    lea rsp, [rel scheduler_kernel_stack_top]
    call scheduler_restore_kernel_segments64
    jmp scheduler_fail
.invalid:
    xor eax, eax
    ret

x86_64_scheduler_quantum_switch64:
    mov byte [rel scheduler_failure_stage], 0xF2
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    jne .invalid
    cmp esi, 1
    jb .invalid
    cmp esi, 4
    ja .invalid
    mov eax, esi
    and eax, 1
    xor eax, 1
    cmp eax, dword [rel scheduler_current_slot]
    jne .invalid
    mov eax, dword [rel scheduler_handoffs]
    inc eax
    cmp eax, esi
    jne .invalid
    mov eax, dword [rel scheduler_current_slot]
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid

    mov rax, qword [rdi + EXCEPTION_FRAME_RAX]
    mov qword [r12 + TASK_RAX], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RBX]
    mov qword [r12 + TASK_RBX], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RCX]
    mov qword [r12 + TASK_RCX], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RDX]
    mov qword [r12 + TASK_RDX], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RBP]
    mov qword [r12 + TASK_RBP], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RSI]
    mov qword [r12 + TASK_RSI], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RDI]
    mov qword [r12 + TASK_RDI], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R8]
    mov qword [r12 + TASK_R8], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R9]
    mov qword [r12 + TASK_R9], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R10]
    mov qword [r12 + TASK_R10], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R11]
    mov qword [r12 + TASK_R11], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R12]
    mov qword [r12 + TASK_R12], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R13]
    mov qword [r12 + TASK_R13], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R14]
    mov qword [r12 + TASK_R14], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_R15]
    mov qword [r12 + TASK_R15], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    mov qword [r12 + TASK_RIP], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RSP]
    mov qword [r12 + TASK_RSP], rax
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    mov qword [r12 + TASK_RFLAGS], rax
    mov dword [rel scheduler_handoffs], esi

    cmp esi, 4
    je .terminal_b
    mov qword [r12 + TASK_STATE], TASK_READY
    cmp dword [rel scheduler_current_slot], 0
    jne .ready_b
    mov al, EVENT_A_READY_AGAIN
    jmp .ready_event
.ready_b:
    mov al, EVENT_B_READY_AGAIN
.ready_event:
    call scheduler_append_event64
    mov edi, dword [rel scheduler_current_slot]
    xor edi, 1
    jmp scheduler_enter_task64

.terminal_b:
    cmp dword [rel scheduler_current_slot], 1
    jne scheduler_fail
    mov qword [r12 + TASK_STATE], TASK_PREEMPTED
    mov al, EVENT_B_PREEMPTED
    call scheduler_append_event64
    inc dword [rel scheduler_fault_count]
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    lea rsp, [rel scheduler_kernel_stack_top]
    call scheduler_restore_kernel_segments64
    call x86_64_timer_quantum_disarm64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_quantum_progress64
    test eax, eax
    jz scheduler_fail
    mov edi, 1
    mov esi, TASK_B_GENERATION
    mov edx, TASK_PREEMPTED
    mov ecx, EVENT_B_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    cmp qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    jne scheduler_fail
    cmp qword [r12 + TASK_STATE], TASK_READY
    jne scheduler_fail
    mov qword [r12 + TASK_R15], 1
    xor edi, edi
    jmp scheduler_enter_task64
.invalid:
    xor eax, eax
    ret

x86_64_scheduler_quantum_validate64:
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    jne .invalid
    mov eax, dword [rel scheduler_current_slot]
    cmp eax, TASK_COUNT
    jae .invalid
    mov edx, eax
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid
    test edx, edx
    jnz .generation_b
    cmp qword [r12 + TASK_GENERATION], TASK_A_GENERATION
    jne .invalid
    jmp .generation_ok
.generation_b:
    cmp qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    jne .invalid
.generation_ok:
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 32
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_CS], USER_CODE_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_SS], USER_DATA_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_RSP], USER_STACK_TOP
    jne .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    test rax, 0x200
    jz .invalid
    test rax, RFLAGS_IRQ_FORBIDDEN
    jnz .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

scheduler_verify_quantum_progress64:
    lea r12, [rel scheduler_tasks]
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    mov rcx, QUANTUM_A_MAGIC
    cmp qword [rax], rcx
    jne .fail
    cmp qword [rax + PROBE_PROGRESS_OFFSET], 1
    jbe .fail
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    mov rcx, QUANTUM_B_MAGIC
    cmp qword [rax], rcx
    jne .fail
    cmp qword [rax + PROBE_PROGRESS_OFFSET], 1
    jbe .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

x86_64_scheduler_timer_preempt64:
    mov byte [rel scheduler_failure_stage], 0xC2
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    jne .invalid
    cmp dword [rel scheduler_current_slot], 1
    jne .invalid
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid
    cmp qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    jne .invalid
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 32
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_CS], USER_CODE_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_SS], USER_DATA_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_RSP], USER_STACK_TOP
    jne .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    test rax, 0x200
    jz .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid
    mov qword [r12 + TASK_STATE], TASK_PREEMPTED
    mov al, EVENT_B_PREEMPTED
    call scheduler_append_event64
    inc dword [rel scheduler_fault_count]
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    lea rsp, [rel scheduler_kernel_stack_top]
    call scheduler_restore_kernel_segments64
    mov byte [rel scheduler_failure_stage], 0xC3
    call x86_64_timer_preemption_disarm64
    test eax, eax
    jz scheduler_fail
    mov edi, 1
    mov esi, TASK_B_GENERATION
    mov edx, TASK_PREEMPTED
    mov ecx, EVENT_B_FREE
    mov byte [rel scheduler_failure_stage], 0xC4
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0xC5
    xor edi, edi
    jmp scheduler_enter_task64
.invalid:
    xor eax, eax
    ret

x86_64_scheduler_timer_validate64:
    mov byte [rel scheduler_failure_stage], 0xC1
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    jne .invalid
    cmp dword [rel scheduler_current_slot], 1
    jne .invalid
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid
    cmp qword [r12 + TASK_GENERATION], TASK_B_GENERATION
    jne .invalid
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 32
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_CS], USER_CODE_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_SS], USER_DATA_SELECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_RSP], USER_STACK_TOP
    jne .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    test rax, 0x200
    jz .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

; Build one private address space. EDI is 0 or 1.
scheduler_build_task64:
    cmp edi, TASK_COUNT
    jae .fail
    mov ebx, edi
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov eax, ebx
    shl rax, 14
    lea r13, [rel scheduler_tables]
    add r13, rax
    mov eax, scheduler_tables
    mov ecx, ebx
    shl rcx, 14
    add rax, rcx
    mov qword [r12 + TASK_CR3], rax

    xor eax, eax
    mov rdi, r13
    mov ecx, (4 * PAGE_SIZE) / 8
    rep stosq
    mov rax, qword [rel pml4_table + (256 * 8)]
    test rax, PAGE_USER
    jnz .fail
    mov qword [r13 + (256 * 8)], rax
    mov rax, qword [rel pml4_table + (511 * 8)]
    test rax, PAGE_USER
    jnz .fail
    mov qword [r13 + (511 * 8)], rax

    mov rax, qword [r12 + TASK_CR3]
    add rax, PAGE_SIZE
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r13], rax
    mov rax, qword [r12 + TASK_CR3]
    add rax, 2 * PAGE_SIZE
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r13 + PAGE_SIZE], rax
    mov rax, qword [r12 + TASK_CR3]
    add rax, 3 * PAGE_SIZE
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r13 + (2 * PAGE_SIZE) + (2 * 8)], rax

    xor ebp, ebp
.page_loop:
    mov ecx, ebp
    call x86_64_elf64_page_flags64
    mov r15d, eax
    mov ecx, ebp
    call x86_64_elf64_page_frame64
    mov r14, rax
    test r15d, r15d
    jnz .mapped
    test r14, r14
    jnz .fail
    jmp .next_page
.mapped:
    test r14, r14
    jz .fail
    test r14, PAGE_SIZE - 1
    jnz .fail
    cmp r14, MANAGED_LIMIT
    jae .fail
    test r15d, ~7
    jnz .fail
    test r15d, PF_R
    jz .fail
    mov eax, r15d
    and eax, PF_W | PF_X
    cmp eax, PF_W | PF_X
    je .fail
    test r15d, PF_W
    jz .share_rx
    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, PAGE_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [r12 + TASK_PRIVATE_FRAMES + rbp * 8], rax
    mov rdx, rax
    mov rsi, DIRECT_MAP_BASE
    add rsi, r14
    mov rdi, DIRECT_MAP_BASE
    add rdi, rdx
    mov ecx, PAGE_SIZE / 8
    rep movsq
    mov r14, rdx
    jmp .map_page
.share_rx:
    test r15d, PF_X
    jz .fail
.map_page:
    mov rax, r14
    or rax, PAGE_PRESENT | PAGE_USER
    test r15d, PF_W
    jz .not_write
    or rax, PAGE_WRITE
.not_write:
    test r15d, PF_X
    jnz .store_page
    mov rdx, PAGE_NX
    or rax, rdx
.store_page:
    mov qword [r13 + (3 * PAGE_SIZE) + rbp * 8], rax
.next_page:
    inc ebp
    cmp ebp, USER_PAGE_COUNT
    jb .page_loop

    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, PAGE_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [r12 + TASK_STACK_FRAME], rax
    mov rdi, DIRECT_MAP_BASE
    add rdi, rax
    xor eax, eax
    mov ecx, PAGE_SIZE / 8
    rep stosq
    mov rax, qword [r12 + TASK_STACK_FRAME]
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, PAGE_NX
    or rax, rdx
    mov qword [r13 + (3 * PAGE_SIZE) + (USER_PAGE_COUNT * 8)], rax

    mov rax, qword [rel scheduler_entry]
    mov qword [r12 + TASK_RIP], rax
    mov qword [r12 + TASK_RSP], USER_STACK_TOP
    mov qword [r12 + TASK_RFLAGS], USER_RFLAGS_INITIAL
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preempt_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_ids
    test ebx, ebx
    jnz .id_b
    mov qword [r12 + TASK_RDI], TASK_A_ID
    mov qword [r12 + TASK_ID], TASK_A_ID
    jmp .done
.id_b:
    mov qword [r12 + TASK_RDI], TASK_B_ID
    mov qword [r12 + TASK_ID], TASK_B_ID
    jmp .done
.preempt_ids:
    or qword [r12 + TASK_RFLAGS], 0x200
    test ebx, ebx
    jnz .preempt_b
    mov qword [r12 + TASK_RDI], TASK_A_PREEMPT_ID
    mov qword [r12 + TASK_ID], TASK_A_PREEMPT_ID
    jmp .done
.preempt_b:
    mov qword [r12 + TASK_RDI], TASK_B_PREEMPT_ID
    mov qword [r12 + TASK_ID], TASK_B_PREEMPT_ID
    jmp .done
.quantum_ids:
    or qword [r12 + TASK_RFLAGS], 0x200
    test ebx, ebx
    jnz .quantum_b
    mov qword [r12 + TASK_RDI], TASK_A_QUANTUM_ID
    mov qword [r12 + TASK_ID], TASK_A_QUANTUM_ID
    jmp .done
.quantum_b:
    mov qword [r12 + TASK_RDI], TASK_B_QUANTUM_ID
    mov qword [r12 + TASK_ID], TASK_B_QUANTUM_ID
.done:
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_verify_isolation64:
    lea r12, [rel scheduler_tasks]
    lea r13, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov rax, qword [r12 + TASK_CR3]
    cmp rax, qword [r13 + TASK_CR3]
    je .fail
    mov rax, qword [r12 + TASK_STACK_FRAME]
    test rax, rax
    jz .fail
    cmp rax, qword [r13 + TASK_STACK_FRAME]
    je .fail
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    cmp rax, qword [r13 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    je .fail
    mov ecx, PROBE_DATA_PAGE_INDEX
    call x86_64_elf64_page_flags64
    cmp eax, PF_R | PF_W
    jne .fail
    xor ebx, ebx
.rx_loop:
    mov ecx, ebx
    call x86_64_elf64_page_flags64
    test eax, eax
    jz .next
    test eax, PF_W
    jnz .next
    test eax, PF_X
    jz .fail
    mov ecx, ebx
    call x86_64_elf64_page_frame64
    test rax, rax
    jz .fail
.next:
    inc ebx
    cmp ebx, USER_PAGE_COUNT
    jb .rx_loop
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; EDI slot, ESI generation, EDX terminal state, CL free-event.
scheduler_reap_terminal64:
    cmp edi, TASK_COUNT
    jae .fail
    mov r15b, cl
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_GENERATION], rsi
    jne .fail
    cmp qword [r12 + TASK_STATE], rdx
    jne .fail
    mov ebx, edi
    call scheduler_release_task_frames64
    test eax, eax
    jz .fail
    mov eax, ebx
    shl rax, 14
    lea rdi, [rel scheduler_tables]
    add rdi, rax
    xor eax, eax
    mov ecx, (4 * PAGE_SIZE) / 8
    rep stosq
    mov rdi, r12
    xor eax, eax
    mov ecx, TASK_RECORD_SIZE / 8
    rep stosq
    mov al, r15b
    call scheduler_append_event64
    inc dword [rel scheduler_reap_count]
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_release_task_frames64:
    xor ebp, ebp
.private_loop:
    mov rdi, qword [r12 + TASK_PRIVATE_FRAMES + rbp * 8]
    test rdi, rdi
    jz .next_private
    call physical_frame_free64
    test eax, eax
    jz .fail
    mov qword [r12 + TASK_PRIVATE_FRAMES + rbp * 8], 0
.next_private:
    inc ebp
    cmp ebp, USER_PAGE_COUNT
    jb .private_loop
    mov rdi, qword [r12 + TASK_STACK_FRAME]
    test rdi, rdi
    jz .done
    call physical_frame_free64
    test eax, eax
    jz .fail
    mov qword [r12 + TASK_STACK_FRAME], 0
.done:
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_append_event64:
    movzx ecx, byte [rel scheduler_event_count]
    cmp ecx, EVENT_CAPACITY
    jae scheduler_fail
    lea rdx, [rel scheduler_events]
    mov byte [rdx + rcx], al
    inc byte [rel scheduler_event_count]
    ret

scheduler_verify_final_events64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_events
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preempt_events
    cmp byte [rel scheduler_event_count], 14
    jne .fail
    jmp .records
.preempt_events:
    cmp byte [rel scheduler_event_count], 10
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_preempt_events]
    mov ecx, 10
    repe cmpsb
    jne .fail
    jmp .task_records
.quantum_events:
    cmp byte [rel scheduler_event_count], 14
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_quantum_events]
    mov ecx, 14
    repe cmpsb
    jne .fail
    jmp .task_records
.records:
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_expected_events]
    mov ecx, 14
    repe cmpsb
    jne .fail
.task_records:
    lea r12, [rel scheduler_tasks]
    xor ebx, ebx
.task_zero_loop:
    cmp qword [r12 + rbx * 8], 0
    jne .fail
    inc ebx
    cmp ebx, (TASK_COUNT * TASK_RECORD_SIZE) / 8
    jb .task_zero_loop
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_setup_syscalls64:
    cmp byte [rel scheduler_syscalls_active], 0
    jne .fail
    mov ecx, IA32_EFER
    call scheduler_read_msr64
    test eax, EFER_SCE
    jnz .fail
    mov ecx, IA32_STAR
    call scheduler_read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_LSTAR
    call scheduler_read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_FMASK
    call scheduler_read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_GS_BASE
    call scheduler_read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_KERNEL_GS_BASE
    call scheduler_read_msr64
    test rax, rax
    jnz .fail
    mov byte [rel scheduler_syscalls_active], 1
    mov ecx, IA32_STAR
    mov rax, STAR_VALUE
    call scheduler_write_msr64
    mov ecx, IA32_LSTAR
    lea rax, [rel scheduler_syscall_entry64]
    call scheduler_write_msr64
    mov ecx, IA32_FMASK
    mov rax, SYSCALL_FMASK
    call scheduler_write_msr64
    mov ecx, IA32_GS_BASE
    xor eax, eax
    call scheduler_write_msr64
    mov ecx, IA32_KERNEL_GS_BASE
    lea rax, [rel scheduler_syscall_context]
    call scheduler_write_msr64
    mov ecx, IA32_EFER
    call scheduler_read_msr64
    or eax, EFER_SCE
    call scheduler_write_msr64
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_disable_syscalls64:
    cmp byte [rel scheduler_syscalls_active], 0
    je .done
    mov ecx, IA32_EFER
    call scheduler_read_msr64
    and eax, ~EFER_SCE
    call scheduler_write_msr64
    mov ecx, IA32_STAR
    xor eax, eax
    call scheduler_write_msr64
    mov ecx, IA32_LSTAR
    xor eax, eax
    call scheduler_write_msr64
    mov ecx, IA32_FMASK
    xor eax, eax
    call scheduler_write_msr64
    mov ecx, IA32_GS_BASE
    xor eax, eax
    call scheduler_write_msr64
    mov ecx, IA32_KERNEL_GS_BASE
    xor eax, eax
    call scheduler_write_msr64
    mov byte [rel scheduler_syscalls_active], 0
.done:
    mov eax, 1
    ret

scheduler_read_msr64:
    rdmsr
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    ret

scheduler_write_msr64:
    mov r8, rax
    mov eax, r8d
    shr r8, 32
    mov edx, r8d
    wrmsr
    ret

scheduler_restore_kernel_segments64:
    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    ret

scheduler_cleanup_common64:
    cli
    mov byte [rel scheduler_cleanup_error], 0
    mov byte [rel scheduler_active], 0
    mov rax, qword [rel scheduler_original_cr3]
    test rax, rax
    jz .cr3_done
    mov cr3, rax
.cr3_done:
    call scheduler_restore_kernel_segments64
    call scheduler_disable_syscalls64
    test eax, eax
    jnz .syscalls_done
    mov byte [rel scheduler_cleanup_error], 1
.syscalls_done:
    xor edi, edi
    call x86_64_exception_set_rsp0
    test eax, eax
    jnz .tss_done
    mov byte [rel scheduler_cleanup_error], 1
.tss_done:
    call x86_64_elf64_release64
    test eax, eax
    jnz .loader_done
    mov byte [rel scheduler_cleanup_error], 1
.loader_done:
    call physical_free_frame_count64
    cmp eax, dword [rel scheduler_initial_free]
    je .count_done
    mov byte [rel scheduler_cleanup_error], 1
.count_done:
    cmp byte [rel scheduler_cleanup_error], 0
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_force_cleanup64:
    cli
    mov byte [rel scheduler_active], 0
    call x86_64_timer_preemption_cancel64
    mov rax, qword [rel scheduler_original_cr3]
    test rax, rax
    jz .original_ready
    mov cr3, rax
.original_ready:
    call scheduler_restore_kernel_segments64
    xor ebx, ebx
.task_loop:
    mov eax, ebx
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    call scheduler_release_task_frames64
    mov eax, ebx
    shl rax, 14
    lea rdi, [rel scheduler_tables]
    add rdi, rax
    xor eax, eax
    mov ecx, (4 * PAGE_SIZE) / 8
    rep stosq
    inc ebx
    cmp ebx, TASK_COUNT
    jb .task_loop
    call scheduler_cleanup_common64
    ret

scheduler_fail:
    mov byte [rel scheduler_active], 0
    call scheduler_force_cleanup64
    lea rsi, [rel scheduler_stage_message]
    call serial_write64
    mov al, byte [rel scheduler_failure_stage]
    call scheduler_hex8_local64
    lea rsi, [rel scheduler_newline]
    call serial_write64
scheduler_return_failure64:
    mov byte [rel scheduler_final_result], 0
scheduler_return64:
    mov rsp, qword [rel scheduler_caller_rsp]
    movzx eax, byte [rel scheduler_final_result]
    ret

scheduler_hex8_local64:
    mov r8b, al
    shr al, 4
    call scheduler_hex_nibble64
    mov al, r8b
    and al, 0x0F
scheduler_hex_nibble64:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp serial_putc64
.digit:
    add al, '0'
    jmp serial_putc64

section .rodata
scheduler_expected_events:
    db EVENT_A_READY, EVENT_B_READY
    db EVENT_A_RUNNING, EVENT_A_READY_AGAIN
    db EVENT_B_RUNNING, EVENT_B_READY_AGAIN
    db EVENT_A_RUNNING, EVENT_A_READY_AGAIN
    db EVENT_B_RUNNING, EVENT_B_FAULTED, EVENT_B_FREE
    db EVENT_A_RUNNING, EVENT_A_EXITED, EVENT_A_FREE
scheduler_preempt_events:
    db EVENT_A_READY, EVENT_B_READY, EVENT_A_RUNNING, EVENT_A_READY_AGAIN
    db EVENT_B_RUNNING, EVENT_B_PREEMPTED, EVENT_B_FREE
    db EVENT_A_RUNNING, EVENT_A_EXITED, EVENT_A_FREE
scheduler_quantum_events:
    db EVENT_A_READY, EVENT_B_READY, EVENT_A_RUNNING, EVENT_A_READY_AGAIN
    db EVENT_B_RUNNING, EVENT_B_READY_AGAIN, EVENT_A_RUNNING
    db EVENT_A_READY_AGAIN, EVENT_B_RUNNING, EVENT_B_PREEMPTED, EVENT_B_FREE
    db EVENT_A_RUNNING, EVENT_A_EXITED, EVENT_A_FREE
scheduler_ok_message db "REIST_X86_64_PROCESS_SCHEDULER_OK", 13, 10, 0
scheduler_preempt_ok_message db "REIST_X86_64_TIMER_PREEMPTION_OK", 13, 10, 0
scheduler_quantum_ok_message db "REIST_X86_64_QUANTUM_SWITCH_OK", 13, 10, 0
scheduler_stage_message db "REIST_X86_64_PROCESS_SCHEDULER_STAGE_", 0
scheduler_newline db 13, 10, 0

section .bss
alignb 16
scheduler_state_begin:
scheduler_syscall_context:
    resq 2
scheduler_original_cr3:
    resq 1
scheduler_entry:
    resq 1
scheduler_b_yields:
    resq 1
scheduler_initial_free:
    resd 1
scheduler_current_slot:
    resd 1
scheduler_handoffs:
    resd 1
scheduler_fault_count:
    resd 1
scheduler_reap_count:
    resd 1
scheduler_active:
    resb 1
scheduler_mode:
    resb 1
scheduler_syscalls_active:
    resb 1
scheduler_cleanup_error:
    resb 1
scheduler_final_result:
    resb 1
scheduler_failure_stage:
    resb 1
scheduler_event_count:
    resb 1
scheduler_events:
    resb EVENT_CAPACITY
alignb 8
syscall_rax: resq 1
syscall_rbx: resq 1
syscall_rcx: resq 1
syscall_rdx: resq 1
syscall_rbp: resq 1
syscall_rsi: resq 1
syscall_rdi: resq 1
syscall_r8:  resq 1
syscall_r9:  resq 1
syscall_r10: resq 1
syscall_r11: resq 1
syscall_r12: resq 1
syscall_r13: resq 1
syscall_r14: resq 1
syscall_r15: resq 1
scheduler_state_end:

scheduler_caller_rsp:
    resq 1

alignb 256
scheduler_tasks:
    resb TASK_COUNT * TASK_RECORD_SIZE

alignb 4096
scheduler_tables:
scheduler_task0_pml4: resb PAGE_SIZE
scheduler_task0_pdpt: resb PAGE_SIZE
scheduler_task0_pd:   resb PAGE_SIZE
scheduler_task0_pt:   resb PAGE_SIZE
scheduler_task1_pml4: resb PAGE_SIZE
scheduler_task1_pdpt: resb PAGE_SIZE
scheduler_task1_pd:   resb PAGE_SIZE
scheduler_task1_pt:   resb PAGE_SIZE

alignb 16
scheduler_kernel_stack_bottom:
    resb 16384
scheduler_kernel_stack_top:
