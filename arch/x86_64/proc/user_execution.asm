; Fixed-capacity x86_64 CPL3, syscall-exit and user-fault containment proof.
; This remains part of the isolated bootstrap and is not a production process.

BITS 64

USER_BASE                 equ 0x00400000
USER_PAGE_COUNT           equ 8
USER_END                  equ 0x00408000
USER_STACK_BASE           equ 0x00408000
USER_STACK_TOP            equ 0x00409000
PAGE_SIZE                 equ 4096
MANAGED_LIMIT             equ 0x08000000
HIGHER_HALF_BASE           equ 0xFFFFFFFF80000000

PAGE_PRESENT              equ 0x001
PAGE_WRITE                equ 0x002
PAGE_USER                 equ 0x004
PAGE_HW_A_MASK            equ 0x020
PAGE_HW_AD_MASK           equ 0x060
PAGE_NX                   equ 0x8000000000000000
PF_X                      equ 1
PF_W                      equ 2
PF_R                      equ 4

KERNEL_DATA_SELECTOR      equ 0x10
USER_DATA_SELECTOR        equ 0x2B
USER_CODE_SELECTOR        equ 0x33
USER_RFLAGS_INITIAL       equ 0x0000000000000002
RFLAGS_FIXED_BIT          equ 0x0000000000000002
RFLAGS_RF                 equ 0x0000000000010000
RFLAGS_SYSCALL_FORBIDDEN  equ 0x00000000003F7F00
RFLAGS_FAULT_FORBIDDEN    equ 0x00000000003E7F00

REIST_SYS_EXIT            equ 9
REIST_SYS_READ            equ 15
REIST_SYS_WRITE           equ 20
REIST_SYS_YIELD           equ 40
EXPECTED_EXIT_STATUS      equ 100
ATTEMPT_EXIT              equ 0
ATTEMPT_FAULT             equ 1
ATTEMPT_SHELL             equ 2
EVENT_NONE                equ 0
EVENT_EXIT                equ 1
EVENT_FAULT               equ 2
EVENT_SHELL_EXIT          equ 3
SHELL_STDIN               equ 0
SHELL_STDOUT              equ 1
SHELL_STDERR              equ 2
SHELL_EXIT_STATUS         equ 0
SHELL_IO_MAX              equ 64
SHELL_EXPECTED_READS      equ 10
SHELL_EXPECTED_WRITES     equ 4
REIST_EAGAIN              equ -11
REIST_ENOSYS              equ -38
COM1_DATA                 equ 0x03F8
COM1_LSR                  equ 0x03FD

IA32_EFER                 equ 0xC0000080
IA32_STAR                 equ 0xC0000081
IA32_LSTAR                equ 0xC0000082
IA32_FMASK                equ 0xC0000084
IA32_GS_BASE              equ 0xC0000101
IA32_KERNEL_GS_BASE       equ 0xC0000102
EFER_SCE                  equ 0x001
STAR_VALUE                equ 0x0020000800000000
SYSCALL_FMASK             equ 0x00000000003F4700

EXCEPTION_FRAME_VECTOR    equ (15 * 8)
EXCEPTION_FRAME_ERROR     equ (EXCEPTION_FRAME_VECTOR + 8)
EXCEPTION_FRAME_RIP       equ (EXCEPTION_FRAME_ERROR + 8)
EXCEPTION_FRAME_CS        equ (EXCEPTION_FRAME_RIP + 8)
EXCEPTION_FRAME_RFLAGS    equ (EXCEPTION_FRAME_CS + 8)
EXCEPTION_FRAME_RSP       equ (EXCEPTION_FRAME_RFLAGS + 8)
EXCEPTION_FRAME_SS        equ (EXCEPTION_FRAME_RSP + 8)

SYSCALL_CONTEXT_KERNEL_RSP equ 0
SYSCALL_CONTEXT_USER_RSP   equ 8

section .text
global x86_64_user_execution_selftest64
global x86_64_user_exception64
global x86_64_user_shell64

extern pml4_table
extern physical_frame_alloc64
extern physical_frame_free64
extern physical_free_frame_count64
extern physical_frame_test_window_clear64
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

x86_64_user_execution_selftest64:
    cli
    cld
    mov qword [rel caller_rsp], rsp
    mov rax, cr3
    mov qword [rel original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne user_execution_fail
    call physical_free_frame_count64
    mov dword [rel initial_free_count], eax

    mov qword [rel user_stack_frame], 0
    mov byte [rel execution_active], 0
    mov byte [rel execution_attempt], ATTEMPT_EXIT
    mov byte [rel execution_event], EVENT_NONE
    mov byte [rel exit_event_count], 0
    mov byte [rel fault_event_count], 0
    mov byte [rel syscall_state_active], 0
    mov dword [rel shell_read_count], 0
    mov dword [rel shell_write_count], 0
    mov byte [rel failure_stage], 1
    mov qword [rel failure_observed], 0
    mov qword [rel failure_expected], 0

    mov byte [rel failure_stage], 2
    call x86_64_elf64_load64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 3
    call physical_frame_alloc64
    test rax, rax
    jz user_execution_fail
    test rax, PAGE_SIZE - 1
    jnz user_execution_fail
    cmp rax, MANAGED_LIMIT
    jae user_execution_fail
    mov qword [rel user_stack_frame], rax

    mov byte [rel failure_stage], 4
    call build_user_page_tables64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 5
    call verify_user_context64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 6
    call setup_syscall_state64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 7
    call verify_syscall_state64
    test eax, eax
    jz user_execution_fail

    mov byte [rel execution_active], 1
    jmp enter_user_attempt64

x86_64_user_shell64:
    cli
    cld
    mov qword [rel caller_rsp], rsp
    mov rax, cr3
    mov qword [rel original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne user_execution_fail
    call physical_free_frame_count64
    mov dword [rel initial_free_count], eax

    mov qword [rel user_stack_frame], 0
    mov byte [rel execution_active], 0
    mov byte [rel execution_attempt], ATTEMPT_SHELL
    mov byte [rel execution_event], EVENT_NONE
    mov byte [rel exit_event_count], 0
    mov byte [rel fault_event_count], 0
    mov byte [rel syscall_state_active], 0
    mov dword [rel shell_read_count], 0
    mov dword [rel shell_write_count], 0
    mov byte [rel failure_stage], 0x50
    mov qword [rel failure_observed], 0
    mov qword [rel failure_expected], 0

    mov edi, 1
    call x86_64_elf64_select_image64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 0x51
    call x86_64_elf64_load64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 0x52
    call physical_frame_alloc64
    test rax, rax
    jz user_execution_fail
    test rax, PAGE_SIZE - 1
    jnz user_execution_fail
    cmp rax, MANAGED_LIMIT
    jae user_execution_fail
    mov qword [rel user_stack_frame], rax

    mov byte [rel failure_stage], 0x53
    call build_user_page_tables64
    test eax, eax
    jz user_execution_fail
    call verify_user_context64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 0x54
    call setup_syscall_state64
    test eax, eax
    jz user_execution_fail
    call verify_syscall_state64
    test eax, eax
    jz user_execution_fail
    mov byte [rel execution_active], 1
    jmp enter_user_attempt64

enter_user_attempt64:
    call physical_frame_test_window_clear64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 0x81
    cmp byte [rel execution_active], 1
    jne user_execution_fail
    mov byte [rel failure_stage], 0x82
    call verify_user_context64
    test eax, eax
    jz user_execution_fail
    mov byte [rel failure_stage], 0x83
    call verify_syscall_state64
    test eax, eax
    jz user_execution_fail

    mov byte [rel failure_stage], 0x84
    lea rax, [rel user_kernel_stack_top]
    mov qword [rel syscall_context + SYSCALL_CONTEXT_KERNEL_RSP], rax
    mov qword [rel syscall_context + SYSCALL_CONTEXT_USER_RSP], 0
    mov rdi, rax
    call x86_64_exception_set_rsp0
    test eax, eax
    jz user_execution_fail

    mov byte [rel failure_stage], 0x85
    call x86_64_elf64_entry64
    test rax, rax
    jz user_execution_fail
    mov qword [rel user_entry_address], rax
    mov byte [rel failure_stage], 0x86
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz user_execution_fail

    mov eax, user_pml4
    mov cr3, rax

    push qword USER_DATA_SELECTOR
    push qword USER_STACK_TOP
    push qword USER_RFLAGS_INITIAL
    push qword USER_CODE_SELECTOR
    push qword [rel user_entry_address]

    mov ax, USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor esi, esi
    movzx edi, byte [rel execution_attempt]
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

; SYSCALL enters on the untrusted user stack. SWAPGS exposes only the two-word
; fixed context, and RSP is replaced before any request register is consumed.
x86_64_syscall_entry64:
    swapgs
    mov qword [gs:SYSCALL_CONTEXT_USER_RSP], rsp
    mov rsp, qword [gs:SYSCALL_CONTEXT_KERNEL_RSP]
    swapgs
    cld

    mov qword [rel observed_syscall_number], rax
    mov qword [rel observed_syscall_status], rdi
    mov qword [rel observed_syscall_rip], rcx
    mov qword [rel observed_syscall_rflags], r11
    mov qword [rel shell_saved_rbx], rbx
    mov qword [rel shell_saved_rbp], rbp
    mov qword [rel shell_saved_rdi], rdi
    mov qword [rel shell_saved_rsi], rsi
    mov qword [rel shell_saved_rdx], rdx
    mov qword [rel shell_saved_r8], r8
    mov qword [rel shell_saved_r9], r9
    mov qword [rel shell_saved_r10], r10
    mov qword [rel shell_saved_r12], r12
    mov qword [rel shell_saved_r13], r13
    mov qword [rel shell_saved_r14], r14
    mov qword [rel shell_saved_r15], r15

    cmp byte [rel execution_active], 1
    jne user_syscall_rejected64
    mov rax, cr3
    cmp rax, qword [rel user_cr3]
    jne user_syscall_rejected64
    cmp byte [rel execution_attempt], ATTEMPT_SHELL
    je shell_syscall_dispatch64
    cmp byte [rel execution_attempt], ATTEMPT_EXIT
    jne user_syscall_rejected64
    cmp qword [rel observed_syscall_number], REIST_SYS_EXIT
    jne user_syscall_rejected64
    cmp qword [rel observed_syscall_status], EXPECTED_EXIT_STATUS
    jne user_syscall_rejected64
    cmp qword [rel syscall_context + SYSCALL_CONTEXT_USER_RSP], USER_STACK_TOP
    jne user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rflags]
    test rax, RFLAGS_FIXED_BIT
    jz user_syscall_rejected64
    test rax, RFLAGS_SYSCALL_FORBIDDEN
    jnz user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rip]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz user_syscall_rejected64

    inc byte [rel exit_event_count]
    cmp byte [rel exit_event_count], 1
    jne user_syscall_rejected64
    mov byte [rel execution_event], EVENT_EXIT
    jmp user_event_to_kernel64

shell_syscall_dispatch64:
    mov rax, qword [rel syscall_context + SYSCALL_CONTEXT_USER_RSP]
    cmp rax, USER_STACK_BASE
    jb user_syscall_rejected64
    cmp rax, USER_STACK_TOP
    ja user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rflags]
    test rax, RFLAGS_FIXED_BIT
    jz user_syscall_rejected64
    test rax, RFLAGS_SYSCALL_FORBIDDEN
    jnz user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rip]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz user_syscall_rejected64

    mov rax, qword [rel observed_syscall_number]
    cmp rax, REIST_SYS_READ
    je shell_handle_read64
    cmp rax, REIST_SYS_WRITE
    je shell_handle_write64
    cmp rax, REIST_SYS_YIELD
    je shell_handle_yield64
    cmp rax, REIST_SYS_EXIT
    je shell_handle_exit64
    mov rax, REIST_ENOSYS
    jmp shell_return_to_user64

shell_handle_read64:
    cmp qword [rel shell_saved_rdi], SHELL_STDIN
    jne user_syscall_rejected64
    cmp qword [rel shell_saved_rdx], 1
    jne user_syscall_rejected64
    mov rax, qword [rel shell_saved_rsi]
    mov edx, 1
    mov ecx, PF_W
    call validate_shell_buffer64
    test eax, eax
    jz user_syscall_rejected64
    mov dx, COM1_LSR
    in al, dx
    test al, 0x01
    jz .not_ready
    mov dx, COM1_DATA
    in al, dx
    mov rdi, qword [rel shell_saved_rsi]
    mov byte [rdi], al
    inc dword [rel shell_read_count]
    cmp dword [rel shell_read_count], SHELL_EXPECTED_READS
    ja user_syscall_rejected64
    mov eax, 1
    jmp shell_return_to_user64
.not_ready:
    mov rax, REIST_EAGAIN
    jmp shell_return_to_user64

shell_handle_write64:
    mov rax, qword [rel shell_saved_rdi]
    cmp rax, SHELL_STDOUT
    je .descriptor_ok
    cmp rax, SHELL_STDERR
    jne user_syscall_rejected64
.descriptor_ok:
    mov rdx, qword [rel shell_saved_rdx]
    test rdx, rdx
    jz user_syscall_rejected64
    cmp rdx, SHELL_IO_MAX
    ja user_syscall_rejected64
    mov rax, qword [rel shell_saved_rsi]
    mov ecx, PF_R
    call validate_shell_buffer64
    test eax, eax
    jz user_syscall_rejected64
    mov r12, qword [rel shell_saved_rsi]
    mov r13, qword [rel shell_saved_rdx]
.write_loop:
    mov al, byte [r12]
    call serial_putc64
    test eax, eax
    jz user_syscall_rejected64
    inc r12
    dec r13
    jnz .write_loop
    inc dword [rel shell_write_count]
    cmp dword [rel shell_write_count], SHELL_EXPECTED_WRITES
    ja user_syscall_rejected64
    mov rax, qword [rel shell_saved_rdx]
    jmp shell_return_to_user64

shell_handle_yield64:
    cmp qword [rel shell_saved_rdi], 0
    jne user_syscall_rejected64
    cmp qword [rel shell_saved_rsi], 0
    jne user_syscall_rejected64
    cmp qword [rel shell_saved_rdx], 0
    jne user_syscall_rejected64
    xor eax, eax
    jmp shell_return_to_user64

shell_handle_exit64:
    cmp qword [rel shell_saved_rdi], SHELL_EXIT_STATUS
    jne user_syscall_rejected64
    cmp qword [rel shell_saved_rsi], 0
    jne user_syscall_rejected64
    cmp qword [rel shell_saved_rdx], 0
    jne user_syscall_rejected64
    cmp dword [rel shell_read_count], SHELL_EXPECTED_READS
    jne user_syscall_rejected64
    cmp dword [rel shell_write_count], SHELL_EXPECTED_WRITES
    jne user_syscall_rejected64
    inc byte [rel exit_event_count]
    cmp byte [rel exit_event_count], 1
    jne user_syscall_rejected64
    mov byte [rel execution_event], EVENT_SHELL_EXIT
    jmp user_event_to_kernel64

; RAX is a user address, RDX is 1..64 bytes and ECX is the required PF_* bit.
validate_shell_buffer64:
    test rdx, rdx
    jz .invalid
    cmp rdx, SHELL_IO_MAX
    ja .invalid
    mov r8, rax
    cmp r8, USER_BASE
    jb .invalid
    mov r9, r8
    add r9, rdx
    jc .invalid
    mov r12d, ecx
    cmp r8, USER_STACK_BASE
    jae .stack_buffer
    cmp r9, USER_END
    ja .invalid
    dec r9
    mov rax, r8
    call x86_64_elf64_address_flags64
    test eax, r12d
    jz .invalid
    mov rax, r9
    call x86_64_elf64_address_flags64
    test eax, r12d
    jz .invalid
    mov eax, 1
    ret
.stack_buffer:
    cmp r9, USER_STACK_TOP
    ja .invalid
    cmp r12d, PF_W
    jne .invalid
    call verify_user_context64
    test eax, eax
    jz .invalid
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

shell_return_to_user64:
    mov qword [rel shell_syscall_result], rax
    mov rax, qword [rel syscall_context + SYSCALL_CONTEXT_USER_RSP]
    cmp rax, USER_STACK_BASE
    jb user_syscall_rejected64
    cmp rax, USER_STACK_TOP
    ja user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rip]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz user_syscall_rejected64
    mov rax, qword [rel observed_syscall_rflags]
    test rax, RFLAGS_FIXED_BIT
    jz user_syscall_rejected64
    test rax, RFLAGS_SYSCALL_FORBIDDEN
    jnz user_syscall_rejected64

    push qword USER_DATA_SELECTOR
    push qword [rel syscall_context + SYSCALL_CONTEXT_USER_RSP]
    push qword [rel observed_syscall_rflags]
    push qword USER_CODE_SELECTOR
    push qword [rel observed_syscall_rip]
    mov ax, USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov rbx, qword [rel shell_saved_rbx]
    mov rbp, qword [rel shell_saved_rbp]
    mov rdi, qword [rel shell_saved_rdi]
    mov rsi, qword [rel shell_saved_rsi]
    mov rdx, qword [rel shell_saved_rdx]
    mov r8, qword [rel shell_saved_r8]
    mov r9, qword [rel shell_saved_r9]
    mov r10, qword [rel shell_saved_r10]
    mov r12, qword [rel shell_saved_r12]
    mov r13, qword [rel shell_saved_r13]
    mov r14, qword [rel shell_saved_r14]
    mov r15, qword [rel shell_saved_r15]
    mov rax, qword [rel shell_syscall_result]
    iretq

user_syscall_rejected64:
    mov byte [rel failure_stage], 0x20
    mov byte [rel execution_event], EVENT_NONE

user_event_to_kernel64:
    mov rax, qword [rel original_cr3]
    mov cr3, rax
    mov rsp, qword [rel syscall_context + SYSCALL_CONTEXT_KERNEL_RSP]
    jmp user_event_return64

; RDI points at the normalized exception frame. Invalid user-looking frames
; return to the common fatal handler; a valid fault abandons only that frame.
x86_64_user_exception64:
    cmp byte [rel execution_active], 1
    jne .invalid
    cmp byte [rel execution_attempt], ATTEMPT_FAULT
    jne .invalid
    mov rax, cr3
    cmp rax, qword [rel user_cr3]
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
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .invalid

    inc byte [rel fault_event_count]
    cmp byte [rel fault_event_count], 1
    jne .invalid
    mov byte [rel execution_event], EVENT_FAULT
    mov rax, qword [rel original_cr3]
    mov cr3, rax
    mov rsp, qword [rel syscall_context + SYSCALL_CONTEXT_KERNEL_RSP]
    jmp user_event_return64
.invalid:
    xor eax, eax
    ret

user_event_return64:
    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    cmp byte [rel execution_attempt], ATTEMPT_SHELL
    je .shell_result
    cmp byte [rel execution_attempt], ATTEMPT_EXIT
    jne .fault_result
    mov byte [rel failure_stage], 0x30
    cmp byte [rel execution_event], EVENT_EXIT
    jne user_execution_fail
    cmp byte [rel exit_event_count], 1
    jne user_execution_fail
    cmp byte [rel fault_event_count], 0
    jne user_execution_fail
    mov byte [rel execution_attempt], ATTEMPT_FAULT
    mov byte [rel execution_event], EVENT_NONE
    jmp enter_user_attempt64

.shell_result:
    mov byte [rel failure_stage], 0x60
    cmp byte [rel execution_event], EVENT_SHELL_EXIT
    jne user_execution_fail
    cmp byte [rel exit_event_count], 1
    jne user_execution_fail
    cmp byte [rel fault_event_count], 0
    jne user_execution_fail
    cmp dword [rel shell_read_count], SHELL_EXPECTED_READS
    jne user_execution_fail
    cmp dword [rel shell_write_count], SHELL_EXPECTED_WRITES
    jne user_execution_fail
    call user_execution_cleanup64
    test eax, eax
    jz user_execution_return_failure64
    lea rsi, [rel shell_exit_ok_message]
    call serial_write64
    mov byte [rel final_result], 1
    jmp user_execution_return64

.fault_result:
    mov byte [rel failure_stage], 0x31
    cmp byte [rel execution_attempt], ATTEMPT_FAULT
    jne user_execution_fail
    cmp byte [rel execution_event], EVENT_FAULT
    jne user_execution_fail
    cmp byte [rel exit_event_count], 1
    jne user_execution_fail
    cmp byte [rel fault_event_count], 1
    jne user_execution_fail

    mov byte [rel failure_stage], 0x40
    call user_execution_cleanup64
    test eax, eax
    jz user_execution_return_failure64
    lea rsi, [rel user_execution_ok_message]
    call serial_write64
    mov byte [rel final_result], 1
    jmp user_execution_return64

user_execution_fail:
    call user_execution_cleanup64
    lea rsi, [rel user_execution_stage_message]
    call serial_write64
    mov al, byte [rel failure_stage]
    call serial_hex8_local64
    lea rsi, [rel observed_message]
    call serial_write64
    mov rax, qword [rel failure_observed]
    call serial_hex64_local64
    lea rsi, [rel expected_message]
    call serial_write64
    mov rax, qword [rel failure_expected]
    call serial_hex64_local64
    lea rsi, [rel newline_message]
    call serial_write64
user_execution_return_failure64:
    mov byte [rel final_result], 0

user_execution_return64:
    mov rsp, qword [rel caller_rsp]
    movzx eax, byte [rel final_result]
    ret

serial_hex8_local64:
    mov r8b, al
    shr al, 4
    call serial_hex_nibble_local64
    mov al, r8b
    and al, 0x0F
serial_hex_nibble_local64:
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp serial_putc64
.digit:
    add al, '0'
    jmp serial_putc64

serial_hex64_local64:
    mov r9, rax
    mov r10d, 16
.next:
    rol r9, 4
    mov al, r9b
    and al, 0x0F
    call serial_hex_nibble_local64
    dec r10d
    jnz .next
    ret

build_user_page_tables64:
    cld
    xor eax, eax
    lea rdi, [rel user_pml4]
    mov ecx, (4 * PAGE_SIZE) / 8
    rep stosq

    mov rax, qword [rel pml4_table + (256 * 8)]
    test rax, PAGE_PRESENT
    jz .fail
    test rax, PAGE_USER
    jnz .fail
    mov qword [rel user_pml4 + (256 * 8)], rax
    mov rax, qword [rel pml4_table + (511 * 8)]
    test rax, PAGE_PRESENT
    jz .fail
    test rax, PAGE_USER
    jnz .fail
    mov qword [rel user_pml4 + (511 * 8)], rax

    mov eax, user_pdpt
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [rel user_pml4], rax
    mov eax, user_pd
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [rel user_pdpt], rax
    mov eax, user_pt
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov qword [rel user_pd + (2 * 8)], rax

    lea r13, [rel user_pt]
    xor ebx, ebx
    xor r12d, r12d
.elf_page_loop:
    mov ecx, ebx
    call expected_elf_pte64
    jc .fail
    mov qword [r13 + rbx * 8], rax
    test rax, rax
    jz .next_elf_page
    inc r12d
.next_elf_page:
    inc ebx
    cmp ebx, USER_PAGE_COUNT
    jb .elf_page_loop
    test r12d, r12d
    jz .fail

    mov rax, qword [rel user_stack_frame]
    test rax, rax
    jz .fail
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, PAGE_NX
    or rax, rdx
    mov qword [rel user_pt + (USER_PAGE_COUNT * 8)], rax
    mov eax, user_pml4
    mov qword [rel user_cr3], rax
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

expected_elf_pte64:
    call x86_64_elf64_page_frame64
    mov r8, rax
    call x86_64_elf64_page_flags64
    mov r9d, eax
    test r9d, r9d
    jnz .mapped
    test r8, r8
    jnz .invalid
    xor eax, eax
    clc
    ret
.mapped:
    test r8, r8
    jz .invalid
    test r8, PAGE_SIZE - 1
    jnz .invalid
    cmp r8, MANAGED_LIMIT
    jae .invalid
    test r9d, ~7
    jnz .invalid
    test r9d, PF_R
    jz .invalid
    mov eax, r9d
    and eax, PF_W | PF_X
    cmp eax, PF_W | PF_X
    je .invalid
    mov rax, r8
    or rax, PAGE_PRESENT | PAGE_USER
    test r9d, PF_W
    jz .not_writable
    or rax, PAGE_WRITE
.not_writable:
    test r9d, PF_X
    jnz .valid
    mov rdx, PAGE_NX
    or rax, rdx
.valid:
    clc
    ret
.invalid:
    xor eax, eax
    stc
    ret

verify_user_context64:
    mov byte [rel failure_stage], 0x90
    mov rax, qword [rel original_cr3]
    mov edx, pml4_table
    cmp rax, rdx
    jne .fail
    mov rax, qword [rel user_cr3]
    mov edx, user_pml4
    cmp rax, rdx
    jne .fail
    test rax, PAGE_SIZE - 1
    jnz .fail

    mov byte [rel failure_stage], 0x91
    mov eax, user_pdpt
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, qword [rel user_pml4]
    mov r8, rdx
    and rdx, ~PAGE_HW_A_MASK
    cmp rdx, rax
    je .pml4_user_ok
    mov qword [rel failure_observed], r8
    mov qword [rel failure_expected], rax
    jmp .fail
.pml4_user_ok:
    mov byte [rel failure_stage], 0x92
    mov eax, user_pd
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, qword [rel user_pdpt]
    and rdx, ~PAGE_HW_A_MASK
    cmp rdx, rax
    jne .fail
    mov byte [rel failure_stage], 0x93
    mov eax, user_pt
    or eax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, qword [rel user_pd + (2 * 8)]
    and rdx, ~PAGE_HW_A_MASK
    cmp rdx, rax
    jne .fail

    mov byte [rel failure_stage], 0x94
    mov rax, qword [rel user_pml4 + (256 * 8)]
    mov rdx, qword [rel pml4_table + (256 * 8)]
    and rax, ~PAGE_HW_A_MASK
    and rdx, ~PAGE_HW_A_MASK
    cmp rax, rdx
    jne .fail
    test rax, PAGE_USER
    jnz .fail
    mov rax, qword [rel user_pml4 + (511 * 8)]
    mov rdx, qword [rel pml4_table + (511 * 8)]
    and rax, ~PAGE_HW_A_MASK
    and rdx, ~PAGE_HW_A_MASK
    cmp rax, rdx
    jne .fail
    test rax, PAGE_USER
    jnz .fail

    mov byte [rel failure_stage], 0x95
    lea r13, [rel user_pt]
    xor ebx, ebx
.verify_elf_loop:
    mov ecx, ebx
    call expected_elf_pte64
    jc .fail
    mov rdx, qword [r13 + rbx * 8]
    and rdx, ~PAGE_HW_AD_MASK
    cmp rdx, rax
    jne .fail
    inc ebx
    cmp ebx, USER_PAGE_COUNT
    jb .verify_elf_loop

    mov byte [rel failure_stage], 0x96
    mov rax, qword [rel user_stack_frame]
    or rax, PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    mov rdx, PAGE_NX
    or rax, rdx
    mov rdx, qword [rel user_pt + (USER_PAGE_COUNT * 8)]
    and rdx, ~PAGE_HW_AD_MASK
    cmp rdx, rax
    jne .fail

    mov byte [rel failure_stage], 0x97
    mov ecx, USER_PAGE_COUNT + 1
.zero_pt_loop:
    cmp qword [r13 + rcx * 8], 0
    jne .fail
    inc ecx
    cmp ecx, 512
    jb .zero_pt_loop
    mov byte [rel failure_stage], 0x98
    lea r13, [rel user_pdpt]
    mov ecx, 1
.zero_pdpt_loop:
    cmp qword [r13 + rcx * 8], 0
    jne .fail
    inc ecx
    cmp ecx, 512
    jb .zero_pdpt_loop
    mov byte [rel failure_stage], 0x99
    lea r13, [rel user_pd]
    xor ecx, ecx
.zero_pd_loop:
    cmp ecx, 2
    je .next_pd
    cmp qword [r13 + rcx * 8], 0
    jne .fail
.next_pd:
    inc ecx
    cmp ecx, 512
    jb .zero_pd_loop
    mov byte [rel failure_stage], 0x9A
    lea r13, [rel user_pml4]
    xor ecx, ecx
.zero_pml4_loop:
    cmp ecx, 0
    je .next_pml4
    cmp ecx, 256
    je .next_pml4
    cmp ecx, 511
    je .next_pml4
    cmp qword [r13 + rcx * 8], 0
    jne .fail
.next_pml4:
    inc ecx
    cmp ecx, 512
    jb .zero_pml4_loop

    mov byte [rel failure_stage], 0x9B
    call x86_64_elf64_entry64
    cmp rax, USER_BASE
    jb .fail
    cmp rax, USER_STACK_BASE
    jae .fail
    call x86_64_elf64_address_flags64
    test eax, PF_X
    jz .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

setup_syscall_state64:
    cmp byte [rel syscall_state_active], 0
    jne .fail
    mov ecx, IA32_EFER
    call read_msr64
    test eax, EFER_SCE
    jnz .fail
    mov ecx, IA32_STAR
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_LSTAR
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_FMASK
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_GS_BASE
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_KERNEL_GS_BASE
    call read_msr64
    test rax, rax
    jnz .fail

    mov byte [rel syscall_state_active], 1
    mov ecx, IA32_STAR
    mov rax, STAR_VALUE
    call write_msr64
    mov ecx, IA32_LSTAR
    lea rax, [rel x86_64_syscall_entry64]
    call write_msr64
    mov ecx, IA32_FMASK
    mov rax, SYSCALL_FMASK
    call write_msr64
    mov ecx, IA32_GS_BASE
    xor eax, eax
    call write_msr64
    mov ecx, IA32_KERNEL_GS_BASE
    lea rax, [rel syscall_context]
    call write_msr64
    mov ecx, IA32_EFER
    call read_msr64
    or eax, EFER_SCE
    call write_msr64
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

verify_syscall_state64:
    cmp byte [rel syscall_state_active], 1
    jne .fail
    mov ecx, IA32_EFER
    call read_msr64
    test eax, EFER_SCE
    jz .fail
    mov ecx, IA32_STAR
    call read_msr64
    mov rdx, STAR_VALUE
    cmp rax, rdx
    jne .fail
    mov ecx, IA32_LSTAR
    call read_msr64
    lea rdx, [rel x86_64_syscall_entry64]
    cmp rax, rdx
    jne .fail
    mov ecx, IA32_FMASK
    call read_msr64
    cmp rax, SYSCALL_FMASK
    jne .fail
    mov ecx, IA32_GS_BASE
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_KERNEL_GS_BASE
    call read_msr64
    lea rdx, [rel syscall_context]
    cmp rax, rdx
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

disable_syscall_state64:
    cmp byte [rel syscall_state_active], 0
    je .already_disabled
    mov ecx, IA32_EFER
    call read_msr64
    and eax, ~EFER_SCE
    call write_msr64
    mov ecx, IA32_STAR
    xor eax, eax
    call write_msr64
    mov ecx, IA32_LSTAR
    xor eax, eax
    call write_msr64
    mov ecx, IA32_FMASK
    xor eax, eax
    call write_msr64
    mov ecx, IA32_GS_BASE
    xor eax, eax
    call write_msr64
    mov ecx, IA32_KERNEL_GS_BASE
    xor eax, eax
    call write_msr64

    mov ecx, IA32_EFER
    call read_msr64
    test eax, EFER_SCE
    jnz .fail
    mov ecx, IA32_STAR
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_LSTAR
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_FMASK
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_GS_BASE
    call read_msr64
    test rax, rax
    jnz .fail
    mov ecx, IA32_KERNEL_GS_BASE
    call read_msr64
    test rax, rax
    jnz .fail
    mov byte [rel syscall_state_active], 0
.already_disabled:
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

read_msr64:
    rdmsr
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    ret

write_msr64:
    mov r8, rax
    mov eax, r8d
    shr r8, 32
    mov edx, r8d
    wrmsr
    ret

user_execution_cleanup64:
    cli
    call physical_frame_test_window_clear64
    mov byte [rel cleanup_error], 0
    mov byte [rel execution_active], 0
    mov rax, qword [rel original_cr3]
    test rax, rax
    jz .cr3_done
    mov cr3, rax
.cr3_done:
    call disable_syscall_state64
    test eax, eax
    jnz .syscall_done
    mov byte [rel cleanup_error], 1
.syscall_done:
    xor edi, edi
    call x86_64_exception_set_rsp0
    test eax, eax
    jnz .tss_done
    mov byte [rel cleanup_error], 1
.tss_done:
    cld
    xor eax, eax
    lea rdi, [rel user_pml4]
    mov ecx, (4 * PAGE_SIZE) / 8
    rep stosq
    mov qword [rel user_cr3], 0
    mov qword [rel user_entry_address], 0

    mov rdi, qword [rel user_stack_frame]
    test rdi, rdi
    jz .stack_done
    call physical_frame_free64
    test eax, eax
    jnz .stack_freed
    mov byte [rel cleanup_error], 1
    jmp .stack_done
.stack_freed:
    mov qword [rel user_stack_frame], 0
.stack_done:
    call x86_64_elf64_release64
    test eax, eax
    jnz .loader_done
    mov byte [rel cleanup_error], 1
.loader_done:
    xor edi, edi
    call x86_64_elf64_select_image64
    test eax, eax
    jnz .selector_done
    mov byte [rel cleanup_error], 1
.selector_done:
    cld
    xor eax, eax
    lea rdi, [rel shell_saved_state_begin]
    mov ecx, (shell_saved_state_end - shell_saved_state_begin) / 8
    rep stosq
    mov dword [rel shell_read_count], 0
    mov dword [rel shell_write_count], 0
    call physical_free_frame_count64
    cmp eax, dword [rel initial_free_count]
    je .count_done
    mov byte [rel cleanup_error], 1
.count_done:
    cmp byte [rel cleanup_error], 0
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

section .rodata
user_execution_ok_message db "REIST_X86_64_USER_EXECUTION_OK", 13, 10, 0
shell_exit_ok_message db "REIST_X86_64_RING3_SHELL_EXIT_OK", 13, 10, 0
user_execution_stage_message db "REIST_X86_64_USER_EXECUTION_STAGE_", 0
observed_message db " observed=", 0
expected_message db " expected=", 0
newline_message db 13, 10, 0

section .bss
alignb 16
syscall_context:
    resq 2
caller_rsp:
    resq 1
original_cr3:
    resq 1
user_cr3:
    resq 1
user_entry_address:
    resq 1
user_stack_frame:
    resq 1
observed_syscall_number:
    resq 1
observed_syscall_status:
    resq 1
observed_syscall_rip:
    resq 1
observed_syscall_rflags:
    resq 1
alignb 8
shell_saved_state_begin:
shell_saved_rbx:
    resq 1
shell_saved_rbp:
    resq 1
shell_saved_rdi:
    resq 1
shell_saved_rsi:
    resq 1
shell_saved_rdx:
    resq 1
shell_saved_r8:
    resq 1
shell_saved_r9:
    resq 1
shell_saved_r10:
    resq 1
shell_saved_r12:
    resq 1
shell_saved_r13:
    resq 1
shell_saved_r14:
    resq 1
shell_saved_r15:
    resq 1
shell_syscall_result:
    resq 1
shell_saved_state_end:
failure_observed:
    resq 1
failure_expected:
    resq 1
initial_free_count:
    resd 1
shell_read_count:
    resd 1
shell_write_count:
    resd 1
execution_active:
    resb 1
execution_attempt:
    resb 1
execution_event:
    resb 1
exit_event_count:
    resb 1
fault_event_count:
    resb 1
syscall_state_active:
    resb 1
cleanup_error:
    resb 1
final_result:
    resb 1
failure_stage:
    resb 1

alignb 4096
user_pml4:
    resb PAGE_SIZE
user_pdpt:
    resb PAGE_SIZE
user_pd:
    resb PAGE_SIZE
user_pt:
    resb PAGE_SIZE

alignb 16
user_kernel_stack_bottom:
    resb 16384
user_kernel_stack_top:
