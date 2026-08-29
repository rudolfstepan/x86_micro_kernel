; Fixed two-process cooperative scheduler proof for the isolated x86_64 image.
; It is deliberately not linked into any production i386 kernel or image.

BITS 64

TASK_COUNT                 equ 2
TASK_SLOT_CAPACITY         equ 4
RUNQUEUE_CAPACITY          equ 4
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
TASK_TABLE_LEVELS          equ 4
TASK_TABLE_BYTES           equ TASK_TABLE_LEVELS * 8
TASK_TABLE_PML4            equ 0
TASK_TABLE_PDPT            equ 8
TASK_TABLE_PD              equ 16
TASK_TABLE_PT              equ 24

TASK_FREE                  equ 0
TASK_READY                 equ 1
TASK_RUNNING               equ 2
TASK_FAULTED               equ 3
TASK_EXITED                equ 4
TASK_PREEMPTED             equ 5
TASK_BLOCKED               equ 6
TASK_WAITING               equ 7
TASK_ZOMBIE                equ 8
TASK_A_GENERATION          equ 1
TASK_B_GENERATION          equ 2
TASK_A_ID                  equ 0x0A
TASK_B_ID                  equ 0x0B
TASK_A_PREEMPT_ID          equ 0x0C
TASK_B_PREEMPT_ID          equ 0x0D
TASK_A_QUANTUM_ID          equ 0x0E
TASK_B_QUANTUM_ID          equ 0x0F
TASK_RUNQUEUE_ID_BASE      equ 0x10
TASK_RUNQUEUE_GEN_BASE     equ 10
TASK_SLEEP_ID_BASE         equ 0x14
TASK_SLEEP_GEN_BASE        equ 20
TASK_DYNAMIC_PARENT_ID     equ 0x18
TASK_DYNAMIC_CHILD_ID      equ 0x19
TASK_DYNAMIC_PARENT_GEN    equ 30
TASK_DYNAMIC_CHILD_GEN1    equ 31
TASK_DYNAMIC_CHILD_GEN2    equ 32
TASK_SHELL_GENERATION      equ 40
TASK_SHELL_CHILD_GEN       equ 41
TASK_DYNAMIC_PARENT_PID    equ 200
TASK_DYNAMIC_CHILD_PID     equ 201
TASK_SHELL_ID              equ 0x1A
TASK_SHELL_CHILD_ID        equ 0x1B
TASK_SHELL_PARENT_PID      equ 300
TASK_SHELL_CHILD_PID       equ 301
TASK_SHELL_CHILD_MODE      equ 1
SCHEDULER_MODE_COOPERATIVE equ 1
SCHEDULER_MODE_PREEMPTION  equ 2
SCHEDULER_MODE_QUANTUM     equ 3
SCHEDULER_MODE_RUNQUEUE    equ 4
SCHEDULER_MODE_SLEEP       equ 5
SCHEDULER_MODE_DYNAMIC     equ 6
SCHEDULER_MODE_SHELL       equ 7

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
EVENT_RUNQUEUE_READY_BASE  equ 0x20
EVENT_RUNQUEUE_RUNNING_BASE equ 0x24
EVENT_RUNQUEUE_YIELD_BASE  equ 0x28
EVENT_RUNQUEUE_EXIT_BASE   equ 0x2C
EVENT_RUNQUEUE_FAULT       equ 0x2F
EVENT_RUNQUEUE_FREE_BASE   equ 0x30
EVENT_SLEEP_READY_BASE     equ 0x40
EVENT_SLEEP_RUNNING_BASE   equ 0x44
EVENT_SLEEP_BLOCK_BASE     equ 0x48
EVENT_SLEEP_WAKE_BASE      equ 0x4C
EVENT_SLEEP_MONOTONIC      equ 0x50
EVENT_SLEEP_EXIT_BASE      equ 0x54
EVENT_SLEEP_FREE_BASE      equ 0x58
EVENT_DYNAMIC_PARENT_READY equ 0x60
EVENT_DYNAMIC_PARENT_RUN   equ 0x61
EVENT_DYNAMIC_REJECT       equ 0x62
EVENT_DYNAMIC_CHILD_READY  equ 0x63
EVENT_DYNAMIC_PARENT_WAIT  equ 0x64
EVENT_DYNAMIC_CHILD_RUN    equ 0x65
EVENT_DYNAMIC_CHILD_EXIT   equ 0x66
EVENT_DYNAMIC_CHILD_FREE   equ 0x67
EVENT_DYNAMIC_PARENT_WAKE  equ 0x68
EVENT_DYNAMIC_PARENT_EXIT  equ 0x69
EVENT_DYNAMIC_PARENT_FREE  equ 0x6A
EVENT_DYNAMIC_SPAWN        equ 0x6B
EVENT_DYNAMIC_GETPID       equ 0x6C
EVENT_SHELL_READY          equ 0x70
EVENT_SHELL_RUNNING        equ 0x71
EVENT_SHELL_EXITED         equ 0x72
EVENT_SHELL_FREE           equ 0x73
EVENT_SHELL_CHILD_FREE     equ 0x74
EVENT_CAPACITY             equ 40
DYNAMIC_EVENT_COUNT        equ 34
DEADLINE_TICK_LIMIT        equ 8
CHILD_PATH_CAPACITY        equ 16

USER_BASE                  equ 0x00400000
USER_PAGE_COUNT            equ 8
USER_STACK_BASE            equ 0x00408000
USER_STACK_TOP             equ 0x00409000
PROBE_DATA_PAGE_INDEX      equ 1
PROBE_FAULT_POINTER_OFFSET equ 8
PROBE_PROGRESS_OFFSET      equ 16
PROBE_RUNQUEUE_FAULT_OFFSET equ 24
QUANTUM_A_MAGIC            equ 0xE44E44E44E44E44E
QUANTUM_B_MAGIC            equ 0xF55F55F55F55F55F
PAGE_SIZE                  equ 4096
USER_END                   equ USER_BASE + (USER_PAGE_COUNT * PAGE_SIZE)
MANAGED_LIMIT              equ 0x08000000
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
REIST_SYS_READ             equ 15
REIST_SYS_WRITE            equ 20
REIST_SYS_YIELD            equ 40
REIST_SYS_SLEEP_MS         equ 41
REIST_SYS_MONOTONIC_MS     equ 42
REIST_SYS_GETPID           equ 22
REIST_SYS_SPAWN            equ 23
REIST_SYS_WAIT             equ 24
EXPECTED_EXIT_STATUS       equ 101
PREEMPT_EXIT_STATUS        equ 102
QUANTUM_EXIT_STATUS        equ 103
RUNQUEUE_EXIT_BASE         equ 110
SLEEP_EXIT_BASE            equ 120
DYNAMIC_PARENT_EXIT_STATUS equ 130
DYNAMIC_CHILD_EXIT_STATUS  equ 77
SHELL_EXIT_STATUS          equ 0
SHELL_EXPECTED_READS       equ 14
SHELL_EXPECTED_WRITES      equ 6
SHELL_STDIN                equ 0
SHELL_STDOUT               equ 1
SHELL_STDERR               equ 2
SHELL_IO_MAX               equ 64
REIST_EAGAIN               equ -11
REIST_ENOSYS               equ -38
COM1_DATA                  equ 0x3F8
COM1_LSR                   equ 0x3FD

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
global x86_64_process_runqueue_selftest64
global x86_64_process_deadline_sleep_selftest64
global x86_64_scheduler_deadline_tick64
global x86_64_process_spawn_wait_selftest64
global x86_64_process_shell64
global x86_64_process_table_metadata_clear64

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
extern x86_64_elf64_select_image64
extern x86_64_exception_set_rsp0
extern serial_write64
extern serial_putc64
extern x86_64_timer_preemption_arm64
extern x86_64_timer_preemption_cancel64
extern x86_64_timer_preemption_disarm64
extern x86_64_timer_quantum_arm64
extern x86_64_timer_quantum_disarm64
extern x86_64_timer_sleep_arm64
extern x86_64_timer_sleep_disarm64
extern x86_64_timer_sleep_now64

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

x86_64_process_deadline_sleep_selftest64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0x70
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
    mov byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
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

    xor ebx, ebx
.build_loop:
    mov edi, ebx
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .build_loop
    call scheduler_verify_runqueue_isolation64
    test eax, eax
    jz scheduler_fail

    mov byte [rel scheduler_failure_stage], 0x71
    xor ebx, ebx
.publish_loop:
    mov eax, ebx
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov eax, TASK_SLEEP_GEN_BASE
    add eax, ebx
    mov qword [r12 + TASK_GENERATION], rax
    mov qword [r12 + TASK_STATE], TASK_READY
    mov eax, ebx
    add al, EVENT_SLEEP_READY_BASE
    call scheduler_append_event64
    mov edi, ebx
    mov esi, TASK_SLEEP_GEN_BASE
    add esi, ebx
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .publish_loop

    ; Duration and generation failures are checked before any deadline byte.
    xor edi, edi
    mov esi, TASK_SLEEP_GEN_BASE
    xor edx, edx
    call scheduler_deadline_insert64
    test eax, eax
    jnz scheduler_fail
    mov rdx, -1
    call scheduler_deadline_insert64
    test eax, eax
    jnz scheduler_fail
    mov edx, 1
    inc esi
    call scheduler_deadline_insert64
    test eax, eax
    jnz scheduler_fail
    cmp byte [rel scheduler_deadline_count], 0
    jne scheduler_fail

    mov byte [rel scheduler_failure_stage], 0x72
    call scheduler_setup_syscalls64
    test eax, eax
    jz scheduler_fail
    lea rax, [rel scheduler_kernel_stack_top]
    mov qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz scheduler_fail
    call x86_64_timer_sleep_arm64
    test eax, eax
    jz scheduler_fail
    xor ebx, ebx
.enable_irq_loop:
    mov eax, ebx
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    or qword [r12 + TASK_RFLAGS], 0x200
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .enable_irq_loop
    mov byte [rel scheduler_active], 1
    mov byte [rel scheduler_failure_stage], 0x73
    call scheduler_runqueue_dispatch64
    jmp scheduler_fail

x86_64_process_spawn_wait_selftest64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0xA8
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
    mov byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
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

    xor edi, edi
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_DYNAMIC_PARENT_READY
    call scheduler_append_event64

    mov byte [rel scheduler_failure_stage], 0xA9
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
    xor edi, edi
    jmp scheduler_enter_task64

; Admit the compact R8.2b shell as one ordinary generation-scoped task. The
; same fixed task builder, queue, syscall gate and reap path used by the
; scheduler proofs own its complete boot-time lifecycle.
x86_64_process_shell64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0x50
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
    mov byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    mov qword [rel scheduler_caller_rsp], rsp
    mov rax, cr3
    mov qword [rel scheduler_original_cr3], rax
    call physical_free_frame_count64
    mov dword [rel scheduler_initial_free], eax

    mov byte [rel scheduler_failure_stage], 0x51
    mov edi, 1
    call x86_64_elf64_select_image64
    test eax, eax
    jz scheduler_fail
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

    mov byte [rel scheduler_failure_stage], 0x52
    xor edi, edi
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    mov qword [r12 + TASK_ID], TASK_SHELL_ID
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_SHELL_READY
    call scheduler_append_event64
    xor edi, edi
    mov esi, TASK_SHELL_GENERATION
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail

    mov byte [rel scheduler_failure_stage], 0x53
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
    call scheduler_runqueue_dispatch64
    jmp scheduler_fail

x86_64_process_runqueue_selftest64:
    cli
    cld
    mov byte [rel scheduler_failure_stage], 0x60
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
    mov byte [rel scheduler_failure_stage], 0x61
    mov byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
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

    xor ebx, ebx
.build_loop:
    mov edi, ebx
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .build_loop
    call scheduler_verify_runqueue_isolation64
    test eax, eax
    jz scheduler_fail

    mov byte [rel scheduler_failure_stage], 0x62
    xor ebx, ebx
.publish_loop:
    mov eax, ebx
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov eax, TASK_RUNQUEUE_GEN_BASE
    add eax, ebx
    mov qword [r12 + TASK_GENERATION], rax
    mov qword [r12 + TASK_STATE], TASK_READY
    mov eax, ebx
    add al, EVENT_RUNQUEUE_READY_BASE
    call scheduler_append_event64
    mov edi, ebx
    mov esi, TASK_RUNQUEUE_GEN_BASE
    add esi, ebx
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .publish_loop

    xor edi, edi
    mov esi, TASK_RUNQUEUE_GEN_BASE
    call scheduler_runqueue_enqueue64
    test eax, eax
    jnz scheduler_fail
    mov edi, 2
    mov esi, TASK_RUNQUEUE_GEN_BASE + 99
    call scheduler_runqueue_enqueue64
    test eax, eax
    jnz scheduler_fail
    call scheduler_verify_initial_runqueue64
    test eax, eax
    jz scheduler_fail

    mov byte [rel scheduler_failure_stage], 0x63
    call scheduler_setup_syscalls64
    test eax, eax
    jz scheduler_fail
    lea rax, [rel scheduler_kernel_stack_top]
    mov qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz scheduler_fail
    call scheduler_enable_user_breakpoint64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_active], 1
    call scheduler_runqueue_dispatch64
    jmp scheduler_fail

; EDI slot, ESI generation. Validation is complete before any queue byte moves.
scheduler_runqueue_enqueue64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne .fail
.mode_valid:
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
    test esi, esi
    jz .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_GENERATION], rsi
    jne .fail
    cmp qword [r12 + TASK_STATE], TASK_READY
    jne .fail
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + rdi], 0
    jne .fail
    movzx ecx, byte [rel scheduler_runqueue_count]
    cmp ecx, RUNQUEUE_CAPACITY
    jae .fail
    movzx ecx, byte [rel scheduler_runqueue_tail]
    cmp ecx, RUNQUEUE_CAPACITY
    jae .fail
    lea rdx, [rel scheduler_runqueue_entries]
    cmp qword [rdx + rcx * 8], 0
    jne .fail
    mov rax, rsi
    shl rax, 32
    mov r10d, edi
    or rax, r10
    mov qword [rdx + rcx * 8], rax
    lea rdx, [rel scheduler_runqueue_membership]
    mov byte [rdx + rdi], 1
    inc ecx
    and ecx, RUNQUEUE_CAPACITY - 1
    mov byte [rel scheduler_runqueue_tail], cl
    inc byte [rel scheduler_runqueue_count]
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; EAX returns slot and EDX generation. The selected entry remains published
; until every task and generation check succeeds.
scheduler_runqueue_dequeue64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .mode_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne .fail
.mode_valid:
    movzx ecx, byte [rel scheduler_runqueue_count]
    test ecx, ecx
    jz .fail
    cmp ecx, RUNQUEUE_CAPACITY
    ja .fail
    movzx ecx, byte [rel scheduler_runqueue_head]
    cmp ecx, RUNQUEUE_CAPACITY
    jae .fail
    lea r12, [rel scheduler_runqueue_entries]
    mov rax, qword [r12 + rcx * 8]
    test rax, rax
    jz .fail
    mov r8d, eax
    shr rax, 32
    mov r9d, eax
    cmp r8d, TASK_SLOT_CAPACITY
    jae .fail
    test r9d, r9d
    jz .fail
    mov eax, r8d
    shl rax, 8
    lea r13, [rel scheduler_tasks]
    add r13, rax
    cmp qword [r13 + TASK_GENERATION], r9
    jne .fail
    cmp qword [r13 + TASK_STATE], TASK_READY
    jne .fail
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + r8], 1
    jne .fail
    mov qword [r12 + rcx * 8], 0
    mov byte [rdx + r8], 0
    inc ecx
    and ecx, RUNQUEUE_CAPACITY - 1
    mov byte [rel scheduler_runqueue_head], cl
    dec byte [rel scheduler_runqueue_count]
    cmp byte [rel scheduler_runqueue_count], 0
    jne .dequeue_done
    mov byte [rel scheduler_runqueue_head], 0
    mov byte [rel scheduler_runqueue_tail], 0
.dequeue_done:
    mov eax, r8d
    mov edx, r9d
    ret
.fail:
    xor eax, eax
    xor edx, edx
    ret

scheduler_runqueue_dispatch64:
    call scheduler_runqueue_dequeue64
    cmp eax, TASK_SLOT_CAPACITY
    jae scheduler_fail
    mov edi, eax
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_GENERATION], rdx
    jne scheduler_fail
    jmp scheduler_enter_task64

scheduler_verify_initial_runqueue64:
    cmp byte [rel scheduler_runqueue_head], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_tail], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_count], RUNQUEUE_CAPACITY
    jne .fail
    xor ebx, ebx
.entry_loop:
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + rbx], 1
    jne .fail
    lea rdx, [rel scheduler_runqueue_entries]
    mov rax, qword [rdx + rbx * 8]
    mov ecx, eax
    cmp ecx, ebx
    jne .fail
    shr rax, 32
    mov ecx, TASK_RUNQUEUE_GEN_BASE
    add ecx, ebx
    cmp eax, ecx
    jne .fail
    inc ebx
    cmp ebx, RUNQUEUE_CAPACITY
    jb .entry_loop
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; EDI slot, ESI generation, RDX absolute PIT tick. All validation precedes
; insertion into the fixed stable deadline array.
scheduler_deadline_insert64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    jne .fail
    test rdx, rdx
    jz .fail
    cmp rdx, DEADLINE_TICK_LIMIT
    ja .fail
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_GENERATION], rsi
    jne .fail
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .fail
    lea r8, [rel scheduler_deadline_membership]
    cmp byte [r8 + rdi], 0
    jne .fail
    movzx ecx, byte [rel scheduler_deadline_count]
    cmp ecx, TASK_SLOT_CAPACITY
    jae .fail
    xor eax, eax
.find:
    cmp eax, ecx
    jae .shift
    mov r9d, eax
    shl r9, 4
    lea r10, [rel scheduler_deadline_entries]
    add r10, r9
    cmp rdx, qword [r10]
    jb .shift
    ja .next
    movzx r9d, byte [r10 + 12]
    cmp edi, r9d
    jb .shift
.next:
    inc eax
    jmp .find
.shift:
    mov r9d, ecx
.shift_loop:
    cmp r9d, eax
    jbe .store
    mov r10d, r9d
    dec r10d
    shl r10, 4
    lea r11, [rel scheduler_deadline_entries]
    add r11, r10
    mov r13, qword [r11]
    mov r14, qword [r11 + 8]
    mov qword [r11 + 16], r13
    mov qword [r11 + 24], r14
    dec r9d
    jmp .shift_loop
.store:
    mov r9d, eax
    shl r9, 4
    lea r10, [rel scheduler_deadline_entries]
    add r10, r9
    mov qword [r10], rdx
    mov dword [r10 + 8], esi
    mov byte [r10 + 12], dil
    mov byte [r10 + 13], 0
    mov word [r10 + 14], 0
    mov byte [r8 + rdi], 1
    inc byte [rel scheduler_deadline_count]
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; ESI is the newly accepted 100-Hz tick. At most four due entries move from
; BLOCKED to READY and into the generation-scoped FIFO.
x86_64_scheduler_deadline_tick64:
    mov byte [rel scheduler_failure_stage], 0x80
    add byte [rel scheduler_failure_stage], sil
    cmp byte [rel scheduler_active], 1
    jne .fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    jne .fail
    mov eax, dword [rel scheduler_last_tick]
    inc eax
    cmp esi, eax
    jne .fail
    cmp esi, DEADLINE_TICK_LIMIT
    ja .fail
    mov ebp, esi
    xor ebx, ebx
.due_loop:
    cmp ebx, TASK_SLOT_CAPACITY
    jae .fail
    movzx ecx, byte [rel scheduler_deadline_count]
    test ecx, ecx
    jz .done
    lea r10, [rel scheduler_deadline_entries]
    cmp qword [r10], rbp
    ja .done
    mov esi, dword [r10 + 8]
    movzx edi, byte [r10 + 12]
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_GENERATION], rsi
    jne .fail
    cmp qword [r12 + TASK_STATE], TASK_BLOCKED
    jne .fail
    lea r8, [rel scheduler_deadline_membership]
    cmp byte [r8 + rdi], 1
    jne .fail
    mov byte [r8 + rdi], 0
    mov r9d, 1
.shift_left:
    cmp r9d, ecx
    jae .clear_last
    mov r11d, r9d
    shl r11, 4
    mov r13, qword [r10 + r11]
    mov r14, qword [r10 + r11 + 8]
    mov qword [r10 + r11 - 16], r13
    mov qword [r10 + r11 - 8], r14
    inc r9d
    jmp .shift_left
.clear_last:
    dec ecx
    mov r9d, ecx
    shl r9, 4
    mov qword [r10 + r9], 0
    mov qword [r10 + r9 + 8], 0
    dec byte [rel scheduler_deadline_count]
    mov qword [r12 + TASK_STATE], TASK_READY
    mov eax, edi
    add al, EVENT_SLEEP_WAKE_BASE
    call scheduler_append_event64
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz .fail
    inc ebx
    jmp .due_loop
.done:
    mov byte [rel scheduler_failure_stage], 0x84
    add byte [rel scheduler_failure_stage], bpl
    mov dword [rel scheduler_last_tick], ebp
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_sleep_dispatch_or_idle64:
    mov byte [rel scheduler_failure_stage], 0x78
    cmp byte [rel scheduler_runqueue_count], 0
    jne scheduler_runqueue_dispatch64
    cmp byte [rel scheduler_deadline_count], 0
    je scheduler_fail
    cmp dword [rel scheduler_idle_wakes], 4
    jae scheduler_fail
    inc dword [rel scheduler_idle_wakes]
    sti
    hlt
    cli
    cmp byte [rel scheduler_runqueue_count], 0
    jne scheduler_runqueue_dispatch64
    mov eax, dword [rel scheduler_final_tick]
    cmp dword [rel scheduler_last_tick], eax
    jae scheduler_fail
    jmp scheduler_sleep_dispatch_or_idle64

scheduler_verify_runqueue_isolation64:
    call scheduler_verify_isolation64
    test eax, eax
    jz .fail
    xor ebx, ebx
.outer:
    mov eax, ebx
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_CR3], 0
    je .fail
    cmp qword [r12 + TASK_STACK_FRAME], 0
    je .fail
    cmp qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)], 0
    je .fail
    mov ebp, ebx
    inc ebp
.inner:
    cmp ebp, TASK_SLOT_CAPACITY
    jae .next_outer
    mov eax, ebp
    shl rax, 8
    lea r13, [rel scheduler_tasks]
    add r13, rax
    mov rax, qword [r12 + TASK_CR3]
    cmp rax, qword [r13 + TASK_CR3]
    je .fail
    mov rax, qword [r12 + TASK_STACK_FRAME]
    cmp rax, qword [r13 + TASK_STACK_FRAME]
    je .fail
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    cmp rax, qword [r13 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    je .fail
    inc ebp
    jmp .inner
.next_outer:
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .outer
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; EDI is the admitted slot. Legacy modes retain two slots; the runqueue mode
; may admit any of its four generation-validated entries.
scheduler_enter_task64:
    cmp byte [rel scheduler_active], 1
    jne scheduler_fail
    cmp edi, TASK_COUNT
    jb .slot_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne scheduler_fail
.extended_slot:
    cmp edi, TASK_SLOT_CAPACITY
    jae scheduler_fail
.slot_valid:
    mov dword [rel scheduler_current_slot], edi
    mov eax, edi
    shl rax, 8
    lea r11, [rel scheduler_tasks]
    add r11, rax
    cmp qword [r11 + TASK_STATE], TASK_READY
    jne scheduler_fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .runqueue_task
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .sleep_task
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .dynamic_task
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je .shell_task
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
    jmp .state_ready
.runqueue_task:
    mov rax, qword [r11 + TASK_GENERATION]
    mov edx, TASK_RUNQUEUE_GEN_BASE
    add edx, edi
    cmp rax, rdx
    jne scheduler_fail
    mov eax, edi
    add al, EVENT_RUNQUEUE_RUNNING_BASE
    jmp .state_ready
.sleep_task:
    mov rax, qword [r11 + TASK_GENERATION]
    mov edx, TASK_SLEEP_GEN_BASE
    add edx, edi
    cmp rax, rdx
    jne scheduler_fail
    mov eax, edi
    add al, EVENT_SLEEP_RUNNING_BASE
    jmp .state_ready
.dynamic_task:
    test edi, edi
    jnz .dynamic_child
    cmp qword [r11 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    mov al, EVENT_DYNAMIC_PARENT_RUN
    jmp .state_ready
.dynamic_child:
    cmp edi, 1
    jne scheduler_fail
    mov eax, dword [rel scheduler_dynamic_child_generation]
    test eax, eax
    jz scheduler_fail
    cmp qword [r11 + TASK_GENERATION], rax
    jne scheduler_fail
    mov al, EVENT_DYNAMIC_CHILD_RUN
    jmp .state_ready
.shell_task:
    cmp edi, 1
    je .shell_child
    test edi, edi
    jnz scheduler_fail
    cmp qword [r11 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp byte [rel scheduler_shell_started], 0
    jne .shell_resume
    mov byte [rel scheduler_shell_started], 1
    mov al, EVENT_SHELL_RUNNING
    jmp .state_ready
.shell_child:
    cmp qword [r11 + TASK_GENERATION], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    mov qword [r11 + TASK_STATE], TASK_RUNNING
    jmp .state_published
.shell_resume:
    mov qword [r11 + TASK_STATE], TASK_RUNNING
    jmp .state_published
.state_ready:
    mov qword [r11 + TASK_STATE], TASK_RUNNING
    call scheduler_append_event64
.state_published:

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
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .quantum_registers
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .quantum_registers
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
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
    jb .syscall_slot_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .syscall_extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .syscall_extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    jne scheduler_fail
.syscall_extended_slot:
    cmp edi, TASK_SLOT_CAPACITY
    jae scheduler_fail
.syscall_slot_valid:
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne scheduler_fail
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne scheduler_fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne .fixed_user_stack
    mov rax, qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_USER_RSP]
    cmp rax, USER_STACK_BASE
    jb scheduler_fail
    cmp rax, USER_STACK_TOP
    ja scheduler_fail
    jmp .user_stack_valid
.fixed_user_stack:
    cmp qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_USER_RSP], USER_STACK_TOP
    jne scheduler_fail
.user_stack_valid:
    mov rax, qword [rel syscall_r11]
    test rax, RFLAGS_FIXED_BIT
    jz scheduler_fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preempt_syscall_flags
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .preempt_syscall_flags
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
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

    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je scheduler_shell_syscall_dispatch64
    cmp qword [rel syscall_rax], REIST_SYS_YIELD
    je scheduler_handle_yield64
    cmp qword [rel syscall_rax], REIST_SYS_SLEEP_MS
    je scheduler_handle_sleep64
    cmp qword [rel syscall_rax], REIST_SYS_MONOTONIC_MS
    je scheduler_handle_monotonic64
    cmp qword [rel syscall_rax], REIST_SYS_GETPID
    je scheduler_handle_getpid64
    cmp qword [rel syscall_rax], REIST_SYS_SPAWN
    je scheduler_handle_spawn64
    cmp qword [rel syscall_rax], REIST_SYS_WAIT
    je scheduler_handle_wait64
    cmp qword [rel syscall_rax], REIST_SYS_EXIT
    je scheduler_handle_exit64
    jmp scheduler_fail

scheduler_save_syscall_context64:
    mov rax, qword [rel syscall_rcx]
    mov qword [r12 + TASK_RIP], rax
    mov rax, qword [rel scheduler_syscall_context + SYSCALL_CONTEXT_USER_RSP]
    mov qword [r12 + TASK_RSP], rax
    mov rax, qword [rel syscall_r11]
    mov qword [r12 + TASK_RFLAGS], rax
    mov qword [r12 + TASK_RAX], 0
    mov rax, qword [rel syscall_rbx]
    mov qword [r12 + TASK_RBX], rax
    mov rax, qword [rel syscall_rcx]
    mov qword [r12 + TASK_RCX], rax
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
    mov rax, qword [rel syscall_r11]
    mov qword [r12 + TASK_R11], rax
    mov rax, qword [rel syscall_r12]
    mov qword [r12 + TASK_R12], rax
    mov rax, qword [rel syscall_r13]
    mov qword [r12 + TASK_R13], rax
    mov rax, qword [rel syscall_r14]
    mov qword [r12 + TASK_R14], rax
    mov rax, qword [rel syscall_r15]
    mov qword [r12 + TASK_R15], rax
    ret

scheduler_shell_syscall_dispatch64:
    cmp qword [rel syscall_rax], REIST_SYS_READ
    je scheduler_handle_shell_read64
    cmp qword [rel syscall_rax], REIST_SYS_WRITE
    je scheduler_handle_shell_write64
    cmp qword [rel syscall_rax], REIST_SYS_YIELD
    je scheduler_handle_shell_yield64
    cmp qword [rel syscall_rax], REIST_SYS_GETPID
    je scheduler_handle_shell_getpid64
    cmp qword [rel syscall_rax], REIST_SYS_SPAWN
    je scheduler_handle_shell_spawn64
    cmp qword [rel syscall_rax], REIST_SYS_WAIT
    je scheduler_handle_shell_wait64
    cmp qword [rel syscall_rax], REIST_SYS_EXIT
    je scheduler_handle_shell_exit64
    mov rax, REIST_ENOSYS
    jmp scheduler_shell_resume64

scheduler_handle_shell_read64:
    cmp qword [rel syscall_rdi], SHELL_STDIN
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 1
    jne scheduler_fail
    mov rax, qword [rel syscall_rsi]
    mov edx, 1
    mov ecx, PF_W
    call scheduler_validate_shell_buffer64
    test eax, eax
    jz scheduler_fail
    mov dx, COM1_LSR
    in al, dx
    test al, 0x01
    jz .not_ready
    mov dx, COM1_DATA
    in al, dx
    mov rdi, qword [rel syscall_rsi]
    mov byte [rdi], al
    inc dword [rel scheduler_shell_read_count]
    cmp dword [rel scheduler_shell_read_count], SHELL_EXPECTED_READS
    ja scheduler_fail
    mov eax, 1
    jmp scheduler_shell_resume64
.not_ready:
    mov rax, REIST_EAGAIN
    jmp scheduler_shell_resume64

scheduler_handle_shell_write64:
    mov rax, qword [rel syscall_rdi]
    cmp rax, SHELL_STDOUT
    je .descriptor_valid
    cmp rax, SHELL_STDERR
    jne scheduler_fail
.descriptor_valid:
    mov rdx, qword [rel syscall_rdx]
    test rdx, rdx
    jz scheduler_fail
    cmp rdx, SHELL_IO_MAX
    ja scheduler_fail
    mov rax, qword [rel syscall_rsi]
    mov ecx, PF_R
    call scheduler_validate_shell_buffer64
    test eax, eax
    jz scheduler_fail
    mov r12, qword [rel syscall_rsi]
    mov r13, qword [rel syscall_rdx]
.write_loop:
    mov al, byte [r12]
    call serial_putc64
    test eax, eax
    jz scheduler_fail
    inc r12
    dec r13
    jnz .write_loop
    inc dword [rel scheduler_shell_write_count]
    cmp dword [rel scheduler_shell_write_count], SHELL_EXPECTED_WRITES
    ja scheduler_fail
    mov rax, qword [rel syscall_rdx]
    jmp scheduler_shell_resume64

scheduler_handle_shell_yield64:
    cmp qword [rel syscall_rdi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rsi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    xor eax, eax
    jmp scheduler_shell_resume64

scheduler_handle_shell_getpid64:
    cmp dword [rel scheduler_current_slot], 0
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp qword [rel syscall_rdi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rsi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    mov rax, TASK_SHELL_PARENT_PID
    jmp scheduler_shell_resume64

scheduler_handle_shell_spawn64:
    mov byte [rel scheduler_failure_stage], 0x55
    cmp dword [rel scheduler_current_slot], 0
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp qword [rel syscall_rsi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    mov rax, qword [rel syscall_rdi]
    call scheduler_validate_child_path64
    test eax, eax
    jz scheduler_fail
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_spawn_count], 0
    jne scheduler_fail
    cmp byte [rel scheduler_runqueue_membership + 1], 0
    jne scheduler_fail
    lea r11, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r11 + TASK_STATE], TASK_FREE
    jne scheduler_fail
    cmp qword [r11 + TASK_GENERATION], 0
    jne scheduler_fail
    cmp qword [r11 + TASK_CR3], 0
    jne scheduler_fail

    call scheduler_save_syscall_context64
    mov edi, 1
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov qword [r12 + TASK_GENERATION], TASK_SHELL_CHILD_GEN
    mov qword [r12 + TASK_ID], TASK_SHELL_CHILD_ID
    mov qword [r12 + TASK_RDI], TASK_SHELL_CHILD_MODE
    mov qword [r12 + TASK_STATE], TASK_READY
    mov byte [rel scheduler_dynamic_child_active], 1
    mov dword [rel scheduler_dynamic_child_generation], TASK_SHELL_CHILD_GEN
    mov dword [rel scheduler_dynamic_parent_generation], TASK_SHELL_GENERATION
    mov dword [rel scheduler_dynamic_spawn_count], 1
    mov edi, 1
    mov esi, TASK_SHELL_CHILD_GEN
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_RAX], TASK_SHELL_CHILD_PID
    mov qword [r12 + TASK_STATE], TASK_READY
    xor edi, edi
    jmp scheduler_enter_task64

scheduler_handle_shell_wait64:
    mov byte [rel scheduler_failure_stage], 0x56
    cmp dword [rel scheduler_current_slot], 0
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp qword [rel syscall_rdi], TASK_SHELL_CHILD_PID
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    cmp byte [rel scheduler_dynamic_child_active], 1
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_child_generation], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_parent_generation], TASK_SHELL_GENERATION
    jne scheduler_fail
    lea r11, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r11 + TASK_GENERATION], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    cmp qword [r11 + TASK_STATE], TASK_READY
    jne scheduler_fail
    mov rax, qword [rel syscall_rsi]
    call scheduler_translate_status_pointer64
    test eax, eax
    jz scheduler_fail
    mov r15, rdx
    call scheduler_save_syscall_context64
    mov qword [r12 + TASK_STATE], TASK_WAITING
    mov dword [rel scheduler_dynamic_wait_generation], TASK_SHELL_GENERATION
    mov dword [rel scheduler_dynamic_wait_child_generation], TASK_SHELL_CHILD_GEN
    mov qword [rel scheduler_dynamic_wait_status_direct], r15
    jmp scheduler_runqueue_dispatch64

; RAX is the syscall result. Reconstruct the task context, publish READY and
; traverse the same generation-validated queue even when it is the sole task.
scheduler_shell_resume64:
    mov r15, rax
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne scheduler_fail
    cmp dword [rel scheduler_current_slot], 0
    jne scheduler_fail
    lea r12, [rel scheduler_tasks]
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    call scheduler_save_syscall_context64
    mov qword [r12 + TASK_RAX], r15
    mov qword [r12 + TASK_STATE], TASK_READY
    xor edi, edi
    mov esi, TASK_SHELL_GENERATION
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    jmp scheduler_runqueue_dispatch64

; Validate one bounded shell buffer before serial or user-memory effects.
; RAX is the user address, RDX the length and ECX exactly PF_R or PF_W.
scheduler_validate_shell_buffer64:
    test rdx, rdx
    jz .invalid
    cmp rdx, SHELL_IO_MAX
    ja .invalid
    cmp ecx, PF_R
    je .permission_valid
    cmp ecx, PF_W
    jne .invalid
.permission_valid:
    mov r8, rax
    cmp r8, USER_BASE
    jb .invalid
    mov r9, r8
    add r9, rdx
    jc .invalid
    mov r14d, ecx
    cmp r8, USER_STACK_BASE
    jae .stack_buffer
    cmp r9, USER_END
    ja .invalid
    dec r9
    mov rax, r8
    call x86_64_elf64_address_flags64
    test eax, r14d
    jz .invalid
    mov rax, r9
    call x86_64_elf64_address_flags64
    test eax, r14d
    jz .invalid
    mov eax, 1
    ret
.stack_buffer:
    cmp r9, USER_STACK_TOP
    ja .invalid
    cmp dword [rel scheduler_current_slot], 0
    jne .invalid
    lea r10, [rel scheduler_tasks]
    cmp qword [r10 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne .invalid
    cmp qword [r10 + TASK_STATE], TASK_RUNNING
    jne .invalid
    cmp qword [r10 + TASK_STACK_FRAME], 0
    je .invalid
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

scheduler_handle_sleep64:
    mov byte [rel scheduler_failure_stage], 0x74
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    jne scheduler_fail
    cmp edi, 3
    jae scheduler_fail
    lea rdx, [rel scheduler_sleep_ms]
    mov rax, qword [rel syscall_rdi]
    cmp rax, qword [rdx + rdi * 8]
    jne scheduler_fail
    call scheduler_save_syscall_context64
    inc qword [r12 + TASK_YIELDS]
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    call x86_64_timer_sleep_now64
    cmp rax, -1
    je scheduler_fail
    lea rdx, [rel scheduler_sleep_ticks]
    mov rdx, qword [rdx + rdi * 8]
    add rdx, rax
    jc scheduler_fail
    mov esi, TASK_SLEEP_GEN_BASE
    add esi, edi
    mov rbx, rdx
    mov r15d, esi
    call scheduler_deadline_insert64
    test eax, eax
    jz scheduler_fail
    ; A second valid publication is rejected by membership before mutation.
    movzx ebp, byte [rel scheduler_deadline_count]
    mov rdx, rbx
    mov esi, r15d
    call scheduler_deadline_insert64
    test eax, eax
    jnz scheduler_fail
    cmp byte [rel scheduler_deadline_count], bpl
    jne scheduler_fail
    test edi, edi
    jnz .deadline_recorded
    mov dword [rel scheduler_final_tick], ebx
.deadline_recorded:
    mov qword [r12 + TASK_STATE], TASK_BLOCKED
    mov eax, edi
    add al, EVENT_SLEEP_BLOCK_BASE
    call scheduler_append_event64
    mov byte [rel scheduler_failure_stage], 0x75
    inc dword [rel scheduler_handoffs]
    jmp scheduler_sleep_dispatch_or_idle64

scheduler_handle_monotonic64:
    mov byte [rel scheduler_failure_stage], 0x76
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    jne scheduler_fail
    cmp edi, 3
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 0
    jne scheduler_fail
    call scheduler_save_syscall_context64
    call x86_64_timer_sleep_now64
    cmp rax, -1
    je scheduler_fail
    mov rdx, 10
    mul rdx
    test rdx, rdx
    jnz scheduler_fail
    cmp rax, qword [rel scheduler_last_monotonic]
    jb scheduler_fail
    mov qword [rel scheduler_last_monotonic], rax
    mov qword [r12 + TASK_RAX], rax
    inc qword [r12 + TASK_YIELDS]
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, EVENT_SLEEP_MONOTONIC
    call scheduler_append_event64
    mov esi, TASK_SLEEP_GEN_BASE + 3
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0x77
    jmp scheduler_runqueue_dispatch64

; RAX points into the current parent's private writable image. The accepted
; path is intentionally fixed for this isolated proof; no VFS is consulted.
scheduler_validate_child_path64:
    test rax, rax
    jz .fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je .shell_stack
    cmp rax, USER_BASE
    jb .fail
    cmp rax, USER_END - CHILD_PATH_CAPACITY
    ja .fail
    test rax, 3
    jnz .fail
    mov rbx, rax
    call x86_64_elf64_address_flags64
    and eax, PF_R | PF_W
    cmp eax, PF_R | PF_W
    jne .fail
    mov rax, rbx
    sub rax, USER_BASE
    mov rcx, rax
    shr rcx, 12
    cmp rcx, USER_PAGE_COUNT
    jae .fail
    cmp qword [r12 + TASK_PRIVATE_FRAMES + rcx * 8], 0
    je .fail
    and eax, PAGE_SIZE - 1
    cmp eax, PAGE_SIZE - CHILD_PATH_CAPACITY
    ja .fail
    lea r8, [rel scheduler_dynamic_child_path]
    jmp .compare_start
.shell_stack:
    cmp rax, USER_STACK_BASE
    jb .fail
    cmp rax, USER_STACK_TOP - CHILD_PATH_CAPACITY
    ja .fail
    test rax, 3
    jnz .fail
    mov rbx, rax
    lea r8, [rel scheduler_shell_child_path]
.compare_start:
    xor ecx, ecx
.compare:
    cmp ecx, CHILD_PATH_CAPACITY
    jae .fail
    mov dl, byte [rbx + rcx]
    cmp dl, byte [r8 + rcx]
    jne .fail
    test dl, dl
    jz .ok
    inc ecx
    jmp .compare
.ok:
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; RAX is a four-byte user status pointer. RDX returns the private direct-map
; address only after bounds, alignment, rights and frame ownership validate.
scheduler_translate_status_pointer64:
    test rax, rax
    jz .fail
    test rax, 3
    jnz .fail
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je .shell_stack
    cmp rax, USER_BASE
    jb .fail
    cmp rax, USER_END - 4
    ja .fail
    mov rbx, rax
    call x86_64_elf64_address_flags64
    test eax, PF_W
    jz .fail
    mov rax, rbx
    sub rax, USER_BASE
    mov rcx, rax
    shr rcx, 12
    cmp rcx, USER_PAGE_COUNT
    jae .fail
    mov rdx, qword [r12 + TASK_PRIVATE_FRAMES + rcx * 8]
    test rdx, rdx
    jz .fail
    mov rcx, DIRECT_MAP_BASE
    add rdx, rcx
    and eax, PAGE_SIZE - 1
    add rdx, rax
    mov eax, 1
    ret
.shell_stack:
    cmp rax, USER_STACK_BASE
    jb .fail
    cmp rax, USER_STACK_TOP - 4
    ja .fail
    mov rdx, qword [r12 + TASK_STACK_FRAME]
    test rdx, rdx
    jz .fail
    mov rcx, DIRECT_MAP_BASE
    add rdx, rcx
    sub rax, USER_STACK_BASE
    add rdx, rax
    mov eax, 1
    ret
.fail:
    xor edx, edx
    xor eax, eax
    ret

; R13 is the signed syscall result and R14b is the bounded event. Request
; validation has completed before this continuation state is written.
scheduler_dynamic_resume64:
    call scheduler_save_syscall_context64
    mov qword [r12 + TASK_RAX], r13
    mov qword [r12 + TASK_STATE], TASK_READY
    mov al, r14b
    call scheduler_append_event64
    xor edi, edi
    jmp scheduler_enter_task64

scheduler_handle_getpid64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    jne scheduler_fail
    test edi, edi
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    mov r13, TASK_DYNAMIC_PARENT_PID
    mov r14b, EVENT_DYNAMIC_GETPID
    jmp scheduler_dynamic_resume64

scheduler_handle_spawn64:
    mov byte [rel scheduler_failure_stage], 0xAA
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    jne scheduler_fail
    test edi, edi
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    mov rax, qword [rel syscall_rdi]
    call scheduler_validate_child_path64
    test eax, eax
    jz .invalid_path
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne .busy
    cmp dword [rel scheduler_dynamic_spawn_count], 2
    jae .busy
    cmp byte [rel scheduler_runqueue_membership + 1], 0
    jne scheduler_fail
    lea r11, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r11 + TASK_STATE], TASK_FREE
    jne scheduler_fail
    cmp qword [r11 + TASK_GENERATION], 0
    jne scheduler_fail
    cmp qword [r11 + TASK_CR3], 0
    jne scheduler_fail

    call scheduler_save_syscall_context64
    mov edi, 1
    call scheduler_build_task64
    test eax, eax
    jz scheduler_fail
    lea r12, [rel scheduler_tasks + TASK_RECORD_SIZE]
    mov eax, TASK_DYNAMIC_CHILD_GEN1
    cmp dword [rel scheduler_dynamic_spawn_count], 0
    je .generation_ready
    mov eax, TASK_DYNAMIC_CHILD_GEN2
.generation_ready:
    mov dword [rel scheduler_dynamic_child_generation], eax
    mov qword [r12 + TASK_GENERATION], rax
    mov qword [r12 + TASK_STATE], TASK_READY
    mov byte [rel scheduler_dynamic_child_active], 1
    mov dword [rel scheduler_dynamic_parent_generation], TASK_DYNAMIC_PARENT_GEN
    mov al, EVENT_DYNAMIC_SPAWN
    call scheduler_append_event64
    mov al, EVENT_DYNAMIC_CHILD_READY
    call scheduler_append_event64
    mov edi, 1
    mov esi, dword [rel scheduler_dynamic_child_generation]
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    inc dword [rel scheduler_dynamic_spawn_count]
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_RAX], TASK_DYNAMIC_CHILD_PID
    mov qword [r12 + TASK_STATE], TASK_READY
    xor edi, edi
    jmp scheduler_enter_task64
.invalid_path:
    mov r13, -14
    mov r14b, EVENT_DYNAMIC_REJECT
    jmp scheduler_dynamic_resume64
.busy:
    mov r13, -16
    mov r14b, EVENT_DYNAMIC_REJECT
    jmp scheduler_dynamic_resume64

scheduler_handle_wait64:
    mov byte [rel scheduler_failure_stage], 0xAB
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    jne scheduler_fail
    test edi, edi
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    cmp qword [rel syscall_rdi], TASK_DYNAMIC_CHILD_PID
    jne .not_child
    cmp byte [rel scheduler_dynamic_child_active], 1
    jne .not_child
    mov eax, dword [rel scheduler_dynamic_child_generation]
    test eax, eax
    jz scheduler_fail
    lea r11, [rel scheduler_tasks + TASK_RECORD_SIZE]
    cmp qword [r11 + TASK_GENERATION], rax
    jne scheduler_fail
    cmp qword [r11 + TASK_STATE], TASK_READY
    jne .not_child
    cmp dword [rel scheduler_dynamic_parent_generation], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    mov rax, qword [rel syscall_rsi]
    call scheduler_translate_status_pointer64
    test eax, eax
    jz .invalid_pointer

    mov r15, rdx
    call scheduler_save_syscall_context64
    mov qword [r12 + TASK_STATE], TASK_WAITING
    mov dword [rel scheduler_dynamic_wait_generation], TASK_DYNAMIC_PARENT_GEN
    mov rax, qword [rel scheduler_dynamic_child_generation]
    mov dword [rel scheduler_dynamic_wait_child_generation], eax
    mov qword [rel scheduler_dynamic_wait_status_direct], r15
    mov al, EVENT_DYNAMIC_PARENT_WAIT
    call scheduler_append_event64
    jmp scheduler_runqueue_dispatch64
.not_child:
    mov r13, -10
    mov r14b, EVENT_DYNAMIC_REJECT
    jmp scheduler_dynamic_resume64
.invalid_pointer:
    mov r13, -14
    mov r14b, EVENT_DYNAMIC_REJECT
    jmp scheduler_dynamic_resume64

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
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .runqueue_yield
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
.runqueue_yield:
    cmp edi, 0
    je .runqueue_yield_slot
    cmp edi, 2
    jne scheduler_fail
.runqueue_yield_slot:
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    mov qword [r12 + TASK_STATE], TASK_READY
    mov eax, edi
    add al, EVENT_RUNQUEUE_YIELD_BASE
    call scheduler_append_event64
    inc dword [rel scheduler_handoffs]
    mov rsi, qword [r12 + TASK_GENERATION]
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    jmp scheduler_runqueue_dispatch64
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
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je scheduler_handle_shell_exit64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je scheduler_handle_runqueue_exit64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je scheduler_handle_sleep_exit64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je scheduler_handle_dynamic_exit64
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

scheduler_handle_shell_exit64:
    mov byte [rel scheduler_failure_stage], 0x54
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne scheduler_fail
    cmp edi, 1
    je scheduler_handle_shell_child_exit64
    cmp edi, 0
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne scheduler_fail
    cmp qword [rel syscall_rdi], SHELL_EXIT_STATUS
    jne scheduler_fail
    cmp qword [rel syscall_rsi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    cmp dword [rel scheduler_shell_read_count], SHELL_EXPECTED_READS
    jne scheduler_fail
    cmp dword [rel scheduler_shell_write_count], SHELL_EXPECTED_WRITES
    jne scheduler_fail
    cmp byte [rel scheduler_runqueue_count], 0
    jne scheduler_fail
    cmp byte [rel scheduler_runqueue_membership], 0
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_spawn_count], 1
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_completed_count], 1
    jne scheduler_fail
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_child_generation], 0
    jne scheduler_fail

    mov qword [r12 + TASK_STATE], TASK_EXITED
    mov al, EVENT_SHELL_EXITED
    call scheduler_append_event64
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov byte [rel scheduler_active], 0
    xor edi, edi
    mov esi, TASK_SHELL_GENERATION
    mov edx, TASK_EXITED
    mov ecx, EVENT_SHELL_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_final_events64
    test eax, eax
    jz scheduler_fail
    call scheduler_cleanup_common64
    test eax, eax
    jz scheduler_return_failure64
    mov byte [rel scheduler_shell_started], 0
    mov dword [rel scheduler_shell_read_count], 0
    mov dword [rel scheduler_shell_write_count], 0
    lea rsi, [rel scheduler_shell_exit_ok_message]
    call serial_write64
    lea rsi, [rel scheduler_shell_ok_message]
    call serial_write64
    mov byte [rel scheduler_final_result], 1
    jmp scheduler_return64

scheduler_handle_shell_child_exit64:
    cmp qword [r12 + TASK_GENERATION], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne scheduler_fail
    cmp qword [rel syscall_rdi], DYNAMIC_CHILD_EXIT_STATUS
    jne scheduler_fail
    cmp qword [rel syscall_rsi], 0
    jne scheduler_fail
    cmp qword [rel syscall_rdx], 0
    jne scheduler_fail
    cmp byte [rel scheduler_dynamic_child_active], 1
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_child_generation], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    lea r11, [rel scheduler_tasks]
    cmp qword [r11 + TASK_STATE], TASK_WAITING
    jne scheduler_fail
    cmp qword [r11 + TASK_GENERATION], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_wait_generation], TASK_SHELL_GENERATION
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_wait_child_generation], TASK_SHELL_CHILD_GEN
    jne scheduler_fail
    mov r15, qword [rel scheduler_dynamic_wait_status_direct]
    test r15, r15
    jz scheduler_fail

    mov qword [r12 + TASK_STATE], TASK_ZOMBIE
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov dword [r15], DYNAMIC_CHILD_EXIT_STATUS
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_RAX], TASK_SHELL_CHILD_PID
    mov qword [r12 + TASK_STATE], TASK_READY
    mov edi, 1
    mov esi, TASK_SHELL_CHILD_GEN
    mov edx, TASK_ZOMBIE
    mov ecx, EVENT_SHELL_CHILD_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_dynamic_child_active], 0
    mov dword [rel scheduler_dynamic_child_generation], 0
    mov dword [rel scheduler_dynamic_parent_generation], 0
    mov dword [rel scheduler_dynamic_wait_generation], 0
    mov dword [rel scheduler_dynamic_wait_child_generation], 0
    mov qword [rel scheduler_dynamic_wait_status_direct], 0
    mov dword [rel scheduler_dynamic_completed_count], 1
    xor edi, edi
    mov esi, TASK_SHELL_GENERATION
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    jmp scheduler_runqueue_dispatch64

scheduler_handle_runqueue_exit64:
    cmp edi, 3
    jae scheduler_fail
    mov ebx, edi
    mov eax, TASK_RUNQUEUE_GEN_BASE
    add eax, ebx
    cmp qword [r12 + TASK_GENERATION], rax
    jne scheduler_fail
    mov eax, RUNQUEUE_EXIT_BASE
    add eax, ebx
    cmp qword [rel syscall_rdi], rax
    jne scheduler_fail
    cmp ebx, 1
    je .expect_no_yield
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    jmp .yield_valid
.expect_no_yield:
    cmp qword [r12 + TASK_YIELDS], 0
    jne scheduler_fail
.yield_valid:
    mov qword [r12 + TASK_STATE], TASK_EXITED
    mov eax, ebx
    add al, EVENT_RUNQUEUE_EXIT_BASE
    call scheduler_append_event64
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov edi, ebx
    call scheduler_verify_runqueue_magic64
    test eax, eax
    jz scheduler_fail
    mov edi, ebx
    mov esi, TASK_RUNQUEUE_GEN_BASE
    add esi, ebx
    mov edx, TASK_EXITED
    mov ecx, EVENT_RUNQUEUE_FREE_BASE
    add ecx, ebx
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    cmp dword [rel scheduler_reap_count], TASK_SLOT_CAPACITY
    je .final
    jmp scheduler_runqueue_dispatch64
.final:
    cmp dword [rel scheduler_handoffs], 2
    jne scheduler_fail
    cmp dword [rel scheduler_fault_count], 1
    jne scheduler_fail
    call scheduler_verify_final_events64
    test eax, eax
    jz scheduler_fail
    call scheduler_cleanup_common64
    test eax, eax
    jz scheduler_return_failure64
    lea rsi, [rel scheduler_runqueue_ok_message]
    call serial_write64
    mov byte [rel scheduler_final_result], 1
    jmp scheduler_return64

scheduler_handle_sleep_exit64:
    mov byte [rel scheduler_failure_stage], 0x90
    add byte [rel scheduler_failure_stage], dil
    cmp edi, TASK_SLOT_CAPACITY
    jae scheduler_fail
    mov ebx, edi
    mov eax, TASK_SLEEP_GEN_BASE
    add eax, ebx
    cmp qword [r12 + TASK_GENERATION], rax
    jne scheduler_fail
    mov eax, SLEEP_EXIT_BASE
    add eax, ebx
    cmp qword [rel syscall_rdi], rax
    jne scheduler_fail
    cmp qword [r12 + TASK_YIELDS], 1
    jne scheduler_fail
    mov qword [r12 + TASK_STATE], TASK_EXITED
    mov eax, ebx
    add al, EVENT_SLEEP_EXIT_BASE
    call scheduler_append_event64
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov edi, ebx
    call scheduler_verify_sleep_magic64
    test eax, eax
    jz scheduler_fail
    mov edi, ebx
    mov esi, TASK_SLEEP_GEN_BASE
    add esi, ebx
    mov edx, TASK_EXITED
    mov ecx, EVENT_SLEEP_FREE_BASE
    add ecx, ebx
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_failure_stage], 0x94
    add byte [rel scheduler_failure_stage], bl
    cmp dword [rel scheduler_reap_count], TASK_SLOT_CAPACITY
    je .final
    jmp scheduler_sleep_dispatch_or_idle64
.final:
    mov byte [rel scheduler_failure_stage], 0x9F
    cmp dword [rel scheduler_handoffs], 3
    jne scheduler_fail
    cmp dword [rel scheduler_fault_count], 0
    jne scheduler_fail
    mov eax, dword [rel scheduler_final_tick]
    test eax, eax
    jz scheduler_fail
    cmp dword [rel scheduler_last_tick], eax
    jne scheduler_fail
    mov edi, eax
    call x86_64_timer_sleep_disarm64
    test eax, eax
    jz scheduler_fail
    call scheduler_verify_final_events64
    test eax, eax
    jz scheduler_fail
    call scheduler_cleanup_common64
    test eax, eax
    jz scheduler_return_failure64
    lea rsi, [rel scheduler_sleep_ok_message]
    call serial_write64
    mov byte [rel scheduler_final_result], 1
    jmp scheduler_return64

scheduler_handle_dynamic_exit64:
    mov byte [rel scheduler_failure_stage], 0xAC
    cmp edi, 1
    je .child
    test edi, edi
    jne scheduler_fail
    cmp qword [r12 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    cmp qword [rel syscall_rdi], DYNAMIC_PARENT_EXIT_STATUS
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_spawn_count], 2
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_completed_count], 2
    jne scheduler_fail
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_child_generation], 0
    jne scheduler_fail
    mov qword [r12 + TASK_STATE], TASK_EXITED
    mov al, EVENT_DYNAMIC_PARENT_EXIT
    call scheduler_append_event64
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    xor edi, edi
    call scheduler_verify_dynamic_magic64
    test eax, eax
    jz scheduler_fail
    xor edi, edi
    mov esi, TASK_DYNAMIC_PARENT_GEN
    mov edx, TASK_EXITED
    mov ecx, EVENT_DYNAMIC_PARENT_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    cmp dword [rel scheduler_reap_count], 3
    jne scheduler_fail
    call scheduler_verify_final_events64
    test eax, eax
    jz scheduler_fail
    call scheduler_cleanup_common64
    test eax, eax
    jz scheduler_return_failure64
    lea rsi, [rel scheduler_dynamic_ok_message]
    call serial_write64
    mov byte [rel scheduler_final_result], 1
    jmp scheduler_return64
.child:
    cmp byte [rel scheduler_dynamic_child_active], 1
    jne scheduler_fail
    mov eax, dword [rel scheduler_dynamic_child_generation]
    test eax, eax
    jz scheduler_fail
    cmp qword [r12 + TASK_GENERATION], rax
    jne scheduler_fail
    cmp qword [rel syscall_rdi], DYNAMIC_CHILD_EXIT_STATUS
    jne scheduler_fail
    lea r11, [rel scheduler_tasks]
    cmp qword [r11 + TASK_STATE], TASK_WAITING
    jne scheduler_fail
    cmp qword [r11 + TASK_GENERATION], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    cmp dword [rel scheduler_dynamic_wait_generation], TASK_DYNAMIC_PARENT_GEN
    jne scheduler_fail
    mov edx, dword [rel scheduler_dynamic_wait_child_generation]
    cmp edx, eax
    jne scheduler_fail
    mov r15, qword [rel scheduler_dynamic_wait_status_direct]
    test r15, r15
    jz scheduler_fail

    mov qword [r12 + TASK_STATE], TASK_ZOMBIE
    mov al, EVENT_DYNAMIC_CHILD_EXIT
    call scheduler_append_event64
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    call scheduler_restore_kernel_segments64
    mov edi, 1
    call scheduler_verify_dynamic_magic64
    test eax, eax
    jz scheduler_fail
    mov dword [r15], DYNAMIC_CHILD_EXIT_STATUS
    lea r12, [rel scheduler_tasks]
    mov qword [r12 + TASK_RAX], TASK_DYNAMIC_CHILD_PID
    mov qword [r12 + TASK_STATE], TASK_READY
    mov edi, 1
    mov esi, dword [rel scheduler_dynamic_child_generation]
    mov edx, TASK_ZOMBIE
    mov ecx, EVENT_DYNAMIC_CHILD_FREE
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    mov byte [rel scheduler_dynamic_child_active], 0
    mov dword [rel scheduler_dynamic_child_generation], 0
    mov dword [rel scheduler_dynamic_parent_generation], 0
    mov dword [rel scheduler_dynamic_wait_generation], 0
    mov dword [rel scheduler_dynamic_wait_child_generation], 0
    mov qword [rel scheduler_dynamic_wait_status_direct], 0
    inc dword [rel scheduler_dynamic_completed_count]
    mov al, EVENT_DYNAMIC_PARENT_WAKE
    call scheduler_append_event64
    xor edi, edi
    mov esi, TASK_DYNAMIC_PARENT_GEN
    call scheduler_runqueue_enqueue64
    test eax, eax
    jz scheduler_fail
    jmp scheduler_runqueue_dispatch64

; RDI points at the normalized exception frame. Only B's exact terminal UD2
; after the third handoff is locally contained; every other frame returns 0.
x86_64_scheduler_user_exception64:
    cmp byte [rel scheduler_active], 1
    jne .invalid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je scheduler_runqueue_user_exception64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_COOPERATIVE
    je .cooperative_exception
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je x86_64_scheduler_timer_abort64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je x86_64_scheduler_timer_abort64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je x86_64_scheduler_timer_abort64
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
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

scheduler_runqueue_user_exception64:
    cmp dword [rel scheduler_current_slot], 3
    jne .invalid
    lea r12, [rel scheduler_tasks + (3 * TASK_RECORD_SIZE)]
    cmp qword [r12 + TASK_STATE], TASK_RUNNING
    jne .invalid
    cmp qword [r12 + TASK_GENERATION], TASK_RUNQUEUE_GEN_BASE + 3
    jne .invalid
    mov rax, cr3
    cmp rax, qword [r12 + TASK_CR3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], 3
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
    test rax, RFLAGS_SYSCALL_FORBIDDEN
    jnz .invalid
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .invalid
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    mov rax, qword [rax + PROBE_RUNQUEUE_FAULT_OFFSET]
    inc rax
    cmp rax, qword [rdi + EXCEPTION_FRAME_RIP]
    jne .invalid
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid
    mov qword [r12 + TASK_STATE], TASK_FAULTED
    mov al, EVENT_RUNQUEUE_FAULT
    call scheduler_append_event64
    inc dword [rel scheduler_fault_count]
    mov rax, qword [rel scheduler_original_cr3]
    mov cr3, rax
    lea rsp, [rel scheduler_kernel_stack_top]
    call scheduler_restore_kernel_segments64
    mov edi, 3
    call scheduler_verify_runqueue_magic64
    test eax, eax
    jz scheduler_fail
    mov edi, 3
    mov esi, TASK_RUNQUEUE_GEN_BASE + 3
    mov edx, TASK_FAULTED
    mov ecx, EVENT_RUNQUEUE_FREE_BASE + 3
    call scheduler_reap_terminal64
    test eax, eax
    jz scheduler_fail
    jmp scheduler_runqueue_dispatch64
.invalid:
    xor eax, eax
    ret

x86_64_scheduler_timer_abort64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .abort
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .abort
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .abort
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
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
    xor r12d, r12d
    cmp edi, TASK_COUNT
    jb .slot_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    jne .fail
.extended_slot:
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
.slot_valid:
    mov ebx, edi
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    cmp qword [r12 + TASK_STATE], TASK_FREE
    jne .fail
    cmp qword [r12 + TASK_CR3], 0
    jne .fail
    mov eax, ebx
    shl rax, 5
    lea r14, [rel scheduler_table_frames]
    add r14, rax
    xor ebp, ebp
.table_metadata_loop:
    cmp qword [r14 + rbp * 8], 0
    jne .fail
    inc ebp
    cmp ebp, TASK_TABLE_LEVELS
    jb .table_metadata_loop
    xor ebp, ebp
.table_allocation_loop:
    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, PAGE_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [r14 + rbp * 8], rax
    inc ebp
    cmp ebp, TASK_TABLE_LEVELS
    jb .table_allocation_loop

    mov r13, DIRECT_MAP_BASE
    add r13, qword [r14 + TASK_TABLE_PML4]
    mov rax, qword [rel pml4_table + (256 * 8)]
    test rax, PAGE_USER
    jnz .fail
    mov qword [r13 + (256 * 8)], rax
    mov rax, qword [rel pml4_table + (511 * 8)]
    test rax, PAGE_USER
    jnz .fail
    mov qword [r13 + (511 * 8)], rax

    mov rax, qword [r14 + TASK_TABLE_PDPT]
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r13], rax
    mov r11, DIRECT_MAP_BASE
    add r11, qword [r14 + TASK_TABLE_PDPT]
    mov rax, qword [r14 + TASK_TABLE_PD]
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r11], rax
    mov r10, DIRECT_MAP_BASE
    add r10, qword [r14 + TASK_TABLE_PD]
    mov rax, qword [r14 + TASK_TABLE_PT]
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [r10 + (2 * 8)], rax

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
    mov edx, ebx
    shl rdx, 5
    lea rdi, [rel scheduler_table_frames]
    add rdi, rdx
    mov rdi, qword [rdi + TASK_TABLE_PT]
    mov rdx, DIRECT_MAP_BASE
    add rdi, rdx
    mov qword [rdi + rbp * 8], rax
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
    mov edx, ebx
    shl rdx, 5
    lea rdi, [rel scheduler_table_frames]
    add rdi, rdx
    mov rdi, qword [rdi + TASK_TABLE_PT]
    mov rdx, DIRECT_MAP_BASE
    add rdi, rdx
    mov qword [rdi + (USER_PAGE_COUNT * 8)], rax

    mov rax, qword [rel scheduler_entry]
    mov qword [r12 + TASK_RIP], rax
    mov qword [r12 + TASK_RSP], USER_STACK_TOP
    mov qword [r12 + TASK_RFLAGS], USER_RFLAGS_INITIAL
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_PREEMPTION
    je .preempt_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_QUANTUM
    je .quantum_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .runqueue_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .sleep_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .dynamic_ids
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je .shell_id
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
    jmp .done
.runqueue_ids:
    mov eax, TASK_RUNQUEUE_ID_BASE
    add eax, ebx
    mov qword [r12 + TASK_RDI], rax
    mov qword [r12 + TASK_ID], rax
    jmp .done
.sleep_ids:
    mov eax, TASK_SLEEP_ID_BASE
    add eax, ebx
    mov qword [r12 + TASK_RDI], rax
    mov qword [r12 + TASK_ID], rax
    jmp .done
.dynamic_ids:
    test ebx, ebx
    jnz .dynamic_child
    mov qword [r12 + TASK_RDI], TASK_DYNAMIC_PARENT_ID
    mov qword [r12 + TASK_ID], TASK_DYNAMIC_PARENT_ID
    jmp .done
.dynamic_child:
    cmp ebx, 1
    jne .fail
    mov qword [r12 + TASK_RDI], TASK_DYNAMIC_CHILD_ID
    mov qword [r12 + TASK_ID], TASK_DYNAMIC_CHILD_ID
    jmp .done
.shell_id:
    cmp ebx, 1
    je .shell_child_id
    test ebx, ebx
    jnz .fail
    mov qword [r12 + TASK_RDI], 0
    mov qword [r12 + TASK_ID], TASK_SHELL_ID
    jmp .done
.shell_child_id:
    mov qword [r12 + TASK_RDI], TASK_SHELL_CHILD_MODE
    mov qword [r12 + TASK_ID], TASK_SHELL_CHILD_ID
.done:
    mov eax, ebx
    shl rax, 5
    lea rdx, [rel scheduler_table_frames]
    add rdx, rax
    mov rax, qword [rdx + TASK_TABLE_PML4]
    test rax, rax
    jz .fail
    mov qword [r12 + TASK_CR3], rax
    mov eax, 1
    ret
.fail:
    call scheduler_release_task_frames64
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

scheduler_verify_runqueue_magic64:
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    lea rdx, [rel scheduler_runqueue_magics]
    mov rdx, qword [rdx + rdi * 8]
    cmp qword [rax], rdx
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_verify_sleep_magic64:
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    lea rdx, [rel scheduler_sleep_magics]
    mov rdx, qword [rdx + rdi * 8]
    cmp qword [rax], rdx
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_verify_dynamic_magic64:
    cmp edi, 1
    ja .fail
    mov eax, edi
    shl rax, 8
    lea r12, [rel scheduler_tasks]
    add r12, rax
    mov rax, qword [r12 + TASK_PRIVATE_FRAMES + (PROBE_DATA_PAGE_INDEX * 8)]
    test rax, rax
    jz .fail
    mov rdx, DIRECT_MAP_BASE
    add rax, rdx
    lea rdx, [rel scheduler_dynamic_magics]
    mov rdx, qword [rdx + rdi * 8]
    cmp qword [rax], rdx
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

; EDI slot, ESI generation, EDX terminal state, CL free-event.
scheduler_reap_terminal64:
    cmp edi, TASK_COUNT
    jb .slot_valid
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .extended_slot
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    jne .fail
.extended_slot:
    cmp edi, TASK_SLOT_CAPACITY
    jae .fail
.slot_valid:
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
    lea rax, [rel scheduler_tasks]
    cmp r12, rax
    jb .fail
    lea rdx, [rel scheduler_tasks + (TASK_SLOT_CAPACITY * TASK_RECORD_SIZE)]
    cmp r12, rdx
    jae .fail
    mov rdx, r12
    sub rdx, rax
    test rdx, TASK_RECORD_SIZE - 1
    jnz .fail
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
    mov rax, r12
    lea rdx, [rel scheduler_tasks]
    sub rax, rdx
    shr rax, 8
    cmp eax, TASK_SLOT_CAPACITY
    jae .fail
    shl rax, 5
    lea r13, [rel scheduler_table_frames]
    add r13, rax
    mov ebp, TASK_TABLE_LEVELS - 1
.table_loop:
    mov rdi, qword [r13 + rbp * 8]
    test rdi, rdi
    jz .next_table
    call physical_frame_free64
    test eax, eax
    jz .fail
    mov qword [r13 + rbp * 8], 0
.next_table:
    dec ebp
    jns .table_loop
    mov qword [r12 + TASK_CR3], 0
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

x86_64_process_table_metadata_clear64:
    cld
    lea rsi, [rel scheduler_table_frames]
    mov ecx, TASK_SLOT_CAPACITY * TASK_TABLE_LEVELS
.metadata_loop:
    cmp qword [rsi], 0
    jne .fail
    add rsi, 8
    loop .metadata_loop
    lea rsi, [rel scheduler_tasks]
    mov ecx, TASK_SLOT_CAPACITY
.task_loop:
    cmp qword [rsi + TASK_CR3], 0
    jne .fail
    add rsi, TASK_RECORD_SIZE
    loop .task_loop
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_verify_final_events64:
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SHELL
    je .shell_events
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_RUNQUEUE
    je .runqueue_events
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_SLEEP
    je .sleep_events
    cmp byte [rel scheduler_mode], SCHEDULER_MODE_DYNAMIC
    je .dynamic_events
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
.runqueue_events:
    cmp byte [rel scheduler_event_count], 20
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_runqueue_events]
    mov ecx, 20
    repe cmpsb
    jne .fail
    cmp byte [rel scheduler_runqueue_head], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_tail], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_count], 0
    jne .fail
    xor ebx, ebx
.runqueue_zero_loop:
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + rbx], 0
    jne .fail
    lea rdx, [rel scheduler_runqueue_entries]
    cmp qword [rdx + rbx * 8], 0
    jne .fail
    inc ebx
    cmp ebx, RUNQUEUE_CAPACITY
    jb .runqueue_zero_loop
    jmp .task_records
.sleep_events:
    cmp byte [rel scheduler_event_count], 27
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_sleep_events]
    mov ecx, 27
    repe cmpsb
    jne .fail
    cmp byte [rel scheduler_runqueue_head], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_tail], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_count], 0
    jne .fail
    cmp byte [rel scheduler_deadline_count], 0
    jne .fail
    xor ebx, ebx
.sleep_zero_loop:
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + rbx], 0
    jne .fail
    lea rdx, [rel scheduler_runqueue_entries]
    cmp qword [rdx + rbx * 8], 0
    jne .fail
    lea rdx, [rel scheduler_deadline_membership]
    cmp byte [rdx + rbx], 0
    jne .fail
    mov eax, ebx
    shl rax, 4
    lea rdx, [rel scheduler_deadline_entries]
    cmp qword [rdx + rax], 0
    jne .fail
    cmp qword [rdx + rax + 8], 0
    jne .fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .sleep_zero_loop
    jmp .task_records
.dynamic_events:
    cmp byte [rel scheduler_event_count], DYNAMIC_EVENT_COUNT
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_dynamic_events]
    mov ecx, DYNAMIC_EVENT_COUNT
    repe cmpsb
    jne .fail
    cmp byte [rel scheduler_runqueue_head], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_tail], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_count], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_spawn_count], 2
    jne .fail
    cmp dword [rel scheduler_dynamic_completed_count], 2
    jne .fail
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_child_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_parent_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_wait_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_wait_child_generation], 0
    jne .fail
    cmp qword [rel scheduler_dynamic_wait_status_direct], 0
    jne .fail
    lea rdx, [rel scheduler_runqueue_membership]
    lea rsi, [rel scheduler_runqueue_entries]
    xor ebx, ebx
.dynamic_queue_zero:
    cmp byte [rdx + rbx], 0
    jne .fail
    cmp qword [rsi + rbx * 8], 0
    jne .fail
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
    jb .dynamic_queue_zero
    jmp .task_records
.shell_events:
    cmp byte [rel scheduler_event_count], 5
    jne .fail
    lea rsi, [rel scheduler_events]
    lea rdi, [rel scheduler_shell_events]
    mov ecx, 5
    repe cmpsb
    jne .fail
    cmp byte [rel scheduler_runqueue_head], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_tail], 0
    jne .fail
    cmp byte [rel scheduler_runqueue_count], 0
    jne .fail
    xor ebx, ebx
.shell_queue_zero:
    lea rdx, [rel scheduler_runqueue_membership]
    cmp byte [rdx + rbx], 0
    jne .fail
    lea rdx, [rel scheduler_runqueue_entries]
    cmp qword [rdx + rbx * 8], 0
    jne .fail
    inc ebx
    cmp ebx, RUNQUEUE_CAPACITY
    jb .shell_queue_zero
    cmp dword [rel scheduler_reap_count], 2
    jne .fail
    cmp byte [rel scheduler_shell_started], 1
    jne .fail
    cmp byte [rel scheduler_dynamic_child_active], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_child_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_parent_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_wait_generation], 0
    jne .fail
    cmp dword [rel scheduler_dynamic_wait_child_generation], 0
    jne .fail
    cmp qword [rel scheduler_dynamic_wait_status_direct], 0
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
    cmp ebx, (TASK_SLOT_CAPACITY * TASK_RECORD_SIZE) / 8
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

; Lease vector 3 to CPL3 only for the bounded runqueue proof. The original
; interrupt-gate attribute is restored on every success and failure path.
scheduler_enable_user_breakpoint64:
    cmp byte [rel scheduler_breakpoint_gate_active], 0
    jne .fail
    sidt [rel scheduler_idtr]
    mov rax, qword [rel scheduler_idtr + 2]
    test rax, rax
    jz .fail
    cmp byte [rax + (3 * 16) + 5], 0x8E
    jne .fail
    mov byte [rax + (3 * 16) + 5], 0xEE
    cmp byte [rax + (3 * 16) + 5], 0xEE
    jne .fail
    mov byte [rel scheduler_breakpoint_gate_active], 1
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

scheduler_disable_user_breakpoint64:
    cmp byte [rel scheduler_breakpoint_gate_active], 0
    je .done
    sidt [rel scheduler_idtr]
    mov rax, qword [rel scheduler_idtr + 2]
    test rax, rax
    jz .fail
    cmp byte [rax + (3 * 16) + 5], 0xEE
    jne .fail
    mov byte [rax + (3 * 16) + 5], 0x8E
    cmp byte [rax + (3 * 16) + 5], 0x8E
    jne .fail
    mov byte [rel scheduler_breakpoint_gate_active], 0
.done:
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
    call scheduler_disable_user_breakpoint64
    test eax, eax
    jnz .breakpoint_done
    mov byte [rel scheduler_cleanup_error], 1
.breakpoint_done:
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
    xor edi, edi
    call x86_64_elf64_select_image64
    test eax, eax
    jnz .selector_done
    mov byte [rel scheduler_cleanup_error], 1
.selector_done:
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
    inc ebx
    cmp ebx, TASK_SLOT_CAPACITY
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
scheduler_runqueue_events:
    db EVENT_RUNQUEUE_READY_BASE + 0, EVENT_RUNQUEUE_READY_BASE + 1
    db EVENT_RUNQUEUE_READY_BASE + 2, EVENT_RUNQUEUE_READY_BASE + 3
    db EVENT_RUNQUEUE_RUNNING_BASE + 0, EVENT_RUNQUEUE_YIELD_BASE + 0
    db EVENT_RUNQUEUE_RUNNING_BASE + 1, EVENT_RUNQUEUE_EXIT_BASE + 1
    db EVENT_RUNQUEUE_FREE_BASE + 1, EVENT_RUNQUEUE_RUNNING_BASE + 2
    db EVENT_RUNQUEUE_YIELD_BASE + 2, EVENT_RUNQUEUE_RUNNING_BASE + 3
    db EVENT_RUNQUEUE_FAULT, EVENT_RUNQUEUE_FREE_BASE + 3
    db EVENT_RUNQUEUE_RUNNING_BASE + 0, EVENT_RUNQUEUE_EXIT_BASE + 0
    db EVENT_RUNQUEUE_FREE_BASE + 0, EVENT_RUNQUEUE_RUNNING_BASE + 2
    db EVENT_RUNQUEUE_EXIT_BASE + 2, EVENT_RUNQUEUE_FREE_BASE + 2
scheduler_runqueue_magics:
    dq 0x1010101010101010, 0x1111111111111111
    dq 0x1212121212121212, 0x1313131313131313
scheduler_sleep_events:
    db EVENT_SLEEP_READY_BASE + 0, EVENT_SLEEP_READY_BASE + 1
    db EVENT_SLEEP_READY_BASE + 2, EVENT_SLEEP_READY_BASE + 3
    db EVENT_SLEEP_RUNNING_BASE + 0, EVENT_SLEEP_BLOCK_BASE + 0
    db EVENT_SLEEP_RUNNING_BASE + 1, EVENT_SLEEP_BLOCK_BASE + 1
    db EVENT_SLEEP_RUNNING_BASE + 2, EVENT_SLEEP_BLOCK_BASE + 2
    db EVENT_SLEEP_RUNNING_BASE + 3, EVENT_SLEEP_MONOTONIC
    db EVENT_SLEEP_RUNNING_BASE + 3, EVENT_SLEEP_EXIT_BASE + 3
    db EVENT_SLEEP_FREE_BASE + 3, EVENT_SLEEP_WAKE_BASE + 1
    db EVENT_SLEEP_RUNNING_BASE + 1, EVENT_SLEEP_EXIT_BASE + 1
    db EVENT_SLEEP_FREE_BASE + 1, EVENT_SLEEP_WAKE_BASE + 2
    db EVENT_SLEEP_RUNNING_BASE + 2, EVENT_SLEEP_EXIT_BASE + 2
    db EVENT_SLEEP_FREE_BASE + 2, EVENT_SLEEP_WAKE_BASE + 0
    db EVENT_SLEEP_RUNNING_BASE + 0, EVENT_SLEEP_EXIT_BASE + 0
    db EVENT_SLEEP_FREE_BASE + 0
scheduler_sleep_ms:
    dq 30, 10, 20, 0
scheduler_sleep_ticks:
    dq 3, 1, 2, 0
scheduler_sleep_magics:
    dq 0x1414141414141414, 0x1515151515151515
    dq 0x1616161616161616, 0x1717171717171717
scheduler_dynamic_events:
    db EVENT_DYNAMIC_PARENT_READY, EVENT_DYNAMIC_PARENT_RUN
    db EVENT_DYNAMIC_GETPID, EVENT_DYNAMIC_PARENT_RUN
    db EVENT_DYNAMIC_REJECT, EVENT_DYNAMIC_PARENT_RUN
    db EVENT_DYNAMIC_SPAWN, EVENT_DYNAMIC_CHILD_READY
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_REJECT
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_REJECT
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_REJECT
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_PARENT_WAIT
    db EVENT_DYNAMIC_CHILD_RUN, EVENT_DYNAMIC_CHILD_EXIT
    db EVENT_DYNAMIC_CHILD_FREE, EVENT_DYNAMIC_PARENT_WAKE
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_REJECT
    db EVENT_DYNAMIC_PARENT_RUN, EVENT_DYNAMIC_SPAWN
    db EVENT_DYNAMIC_CHILD_READY, EVENT_DYNAMIC_PARENT_RUN
    db EVENT_DYNAMIC_PARENT_WAIT, EVENT_DYNAMIC_CHILD_RUN
    db EVENT_DYNAMIC_CHILD_EXIT, EVENT_DYNAMIC_CHILD_FREE
    db EVENT_DYNAMIC_PARENT_WAKE, EVENT_DYNAMIC_PARENT_RUN
    db EVENT_DYNAMIC_PARENT_EXIT, EVENT_DYNAMIC_PARENT_FREE
scheduler_shell_events:
    db EVENT_SHELL_READY, EVENT_SHELL_RUNNING, EVENT_SHELL_CHILD_FREE
    db EVENT_SHELL_EXITED, EVENT_SHELL_FREE
scheduler_dynamic_magics:
    dq 0x1818181818181818, 0x1919191919191919
scheduler_dynamic_child_path db "/probe/child", 0
scheduler_shell_child_path db "/shell/child", 0
scheduler_ok_message db "REIST_X86_64_PROCESS_SCHEDULER_OK", 13, 10, 0
scheduler_preempt_ok_message db "REIST_X86_64_TIMER_PREEMPTION_OK", 13, 10, 0
scheduler_quantum_ok_message db "REIST_X86_64_QUANTUM_SWITCH_OK", 13, 10, 0
scheduler_runqueue_ok_message db "REIST_X86_64_RUNQUEUE_LIFECYCLE_OK", 13, 10, 0
scheduler_sleep_ok_message db "REIST_X86_64_DEADLINE_SLEEP_OK", 13, 10, 0
scheduler_dynamic_ok_message db "REIST_X86_64_SPAWN_WAIT_OK", 13, 10, 0
scheduler_shell_exit_ok_message db "REIST_X86_64_RING3_SHELL_EXIT_OK", 13, 10, 0
scheduler_shell_ok_message db "REIST_X86_64_SCHEDULED_SHELL_OK", 13, 10, 0
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
scheduler_breakpoint_gate_active:
    resb 1
scheduler_runqueue_head:
    resb 1
scheduler_runqueue_tail:
    resb 1
scheduler_runqueue_count:
    resb 1
scheduler_runqueue_membership:
    resb TASK_SLOT_CAPACITY
alignb 8
scheduler_runqueue_entries:
    resq RUNQUEUE_CAPACITY
scheduler_deadline_count:
    resb 1
scheduler_deadline_membership:
    resb TASK_SLOT_CAPACITY
alignb 16
scheduler_deadline_entries:
    resb TASK_SLOT_CAPACITY * 16
scheduler_last_tick:
    resd 1
scheduler_final_tick:
    resd 1
scheduler_idle_wakes:
    resd 1
scheduler_last_monotonic:
    resq 1
scheduler_dynamic_spawn_count:
    resd 1
scheduler_dynamic_completed_count:
    resd 1
scheduler_dynamic_child_generation:
    resd 1
scheduler_dynamic_parent_generation:
    resd 1
scheduler_dynamic_wait_generation:
    resd 1
scheduler_dynamic_wait_child_generation:
    resd 1
scheduler_dynamic_wait_status_direct:
    resq 1
scheduler_dynamic_child_active:
    resb 1
scheduler_shell_started:
    resb 1
scheduler_shell_read_count:
    resd 1
scheduler_shell_write_count:
    resd 1
scheduler_event_count:
    resb 1
scheduler_events:
    resb EVENT_CAPACITY
alignb 8
scheduler_idtr:
    resb 10
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
    resb TASK_SLOT_CAPACITY * TASK_RECORD_SIZE

alignb 32
scheduler_table_frames:
    resq TASK_SLOT_CAPACITY * TASK_TABLE_LEVELS

alignb 16
scheduler_kernel_stack_bottom:
    resb 16384
scheduler_kernel_stack_top:
