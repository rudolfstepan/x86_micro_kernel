; Isolated REIST x86_64 higher-half paging proof.
; Multiboot v1 enters in 32-bit protected mode. No production i386 object
; references this file.

BITS 32

MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ (1 << 0) | (1 << 1)
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
CPUID_ID_BIT        equ (1 << 21)
CPUID_NX_BIT        equ (1 << 20)
CPUID_LONG_MODE_BIT equ (1 << 29)
CR0_WP_BIT          equ (1 << 16)
CR0_PG_BIT          equ (1 << 31)
CR4_PAE_BIT         equ (1 << 5)
EFER_MSR            equ 0xC0000080
EFER_LME_BIT        equ (1 << 8)
EFER_LMA_BIT        equ (1 << 10)
EFER_NXE_BIT        equ (1 << 11)
PAGE_PRESENT        equ 0x001
PAGE_PRESENT_WRITE  equ 0x003
PAGE_2M_FLAGS       equ 0x083
PAGE_NX_HIGH        equ 0x80000000
PAGE_HW_AD_MASK     equ 0x060
HIGHER_HALF_BASE     equ 0xFFFFFFFF80000000
COM1_DATA           equ 0x03F8
COM1_LSR            equ 0x03FD
SERIAL_TX_POLLS     equ 65536
C_HANDOFF_VERSION   equ 1
C_HANDOFF_SIZE      equ 128
C_HANDOFF_FLAGS     equ 0x3FF
C_LIFECYCLE_CAPS    equ 0x0F
C_FIXED_CAPACITY    equ 4
C_SYSCALL_ABI       equ 1
C_HANDOFF_QWORDS    equ C_HANDOFF_SIZE / 8
C_STATE_QWORDS      equ 4
C_CONTROL_VERSION   equ 1
C_CONTROL_SIZE      equ 64
C_CONTROL_FLAGS     equ 0x0F
C_CONTROL_SERVICE   equ 1
C_CONTROL_GENERATION equ 1
C_CONTROL_QWORDS    equ C_CONTROL_SIZE / 8

C_H_VERSION         equ 0
C_H_SIZE            equ 4
C_H_FLAGS           equ 8
C_H_HIGHER_HALF     equ 16
C_H_CR3             equ 24
C_H_PML4            equ 32
C_H_DIRECT_MAP      equ 40
C_H_MANAGED_LIMIT   equ 48
C_H_ELF_BASE        equ 56
C_H_ELF_LIMIT       equ 64
C_H_TEXT_START      equ 72
C_H_TEXT_END        equ 80
C_H_LIFECYCLE       equ 88
C_H_TASK_CAPACITY   equ 96
C_H_RUNQUEUE_CAP    equ 100
C_H_DEADLINE_CAP    equ 104
C_H_SYSCALL_ABI     equ 108

C_C_VERSION         equ 0
C_C_SIZE            equ 4
C_C_FLAGS           equ 8
C_C_GENERATION      equ 16
C_C_SERVICE         equ 24
C_C_TASK_CAPACITY   equ 32
C_C_RUNQUEUE_CAP    equ 36
C_C_SYSCALL_ABI     equ 40

%ifndef C_CORE_TEXT_PATH
    %error "C_CORE_TEXT_PATH is required"
%endif
%ifndef C_CORE_RODATA_PATH
    %error "C_CORE_RODATA_PATH is required"
%endif
%ifndef C_CORE_DATA_PATH
    %error "C_CORE_DATA_PATH is required"
%endif

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

section .text
global x86_64_bootstrap_start
global serial_init64
global serial_write64
global serial_putc64
global halt64
global x86_64_ud2_probe
global x86_64_ud2_resume
global x86_64_nx_probe_target
global x86_64_nx_resume
global pml4_table
global x86_64_c_handoff
global x86_64_c_control_handoff
global x86_64_c_serial_write64
global x86_64_c_process_shell64
extern x86_64_exception_init
extern x86_64_physical_memory_init32
extern x86_64_physical_memory_selftest64
extern physical_frame_test_high_window64
extern physical_frame_test_window_clear64
extern physical_frame_test_window_is_clear64
extern x86_64_elf64_loader_selftest64
extern x86_64_user_execution_selftest64
extern x86_64_user_execution_table_metadata_clear64
extern x86_64_process_scheduler_selftest64
extern x86_64_timer_interrupt_selftest64
extern x86_64_process_preemption_selftest64
extern x86_64_process_quantum_selftest64
extern x86_64_process_runqueue_selftest64
extern x86_64_process_deadline_sleep_selftest64
extern x86_64_process_spawn_wait_selftest64
extern x86_64_process_shell64
extern x86_64_process_table_metadata_clear64
extern _text_start
extern _text_end
extern _rodata_start
extern _rodata_end
extern _data_start
extern _data_end
extern _bss_start
extern _bss_end
extern _c_core_bridge_start
extern _c_core_bridge_end
extern _c_core_text_start
extern _c_core_text_end
extern _c_core_rodata_start
extern _c_core_rodata_end
extern _c_core_data_start
extern _c_core_data_end
extern _c_core_bss_start
extern _c_core_bss_end

x86_64_bootstrap_start:
    cli
    mov dword [boot_magic_value], eax
    mov dword [boot_info_value], ebx
    mov esp, bootstrap_stack_top
    xor ebp, ebp

    call cpu_has_long_mode
    test eax, eax
    jz .unsupported

    ; Multiboot does not promise zeroed BSS. Clear every fixed paging page
    ; before publishing any hierarchy entry.
    xor eax, eax
    mov edi, pml4_table
    mov ecx, (5 * 4096) / 4
    rep stosd

    ; The C ABI requires a loader-independent BSS initialization proof.
    mov edi, x86_64_c_bss_state
    mov ecx, (C_STATE_QWORDS * 8) / 4
    rep stosd
    mov byte [c_control_active], 0

    mov eax, dword [boot_magic_value]
    mov ebx, dword [boot_info_value]
    call x86_64_physical_memory_init32
    test eax, eax
    jz .memory_map_error

    mov eax, pdpt_table
    or eax, PAGE_PRESENT_WRITE
    mov dword [pml4_table], eax
    mov dword [pml4_table + (511 * 8)], eax

    mov eax, low_page_directory
    or eax, PAGE_PRESENT_WRITE
    mov dword [pdpt_table], eax

    mov eax, high_page_directory
    or eax, PAGE_PRESENT_WRITE
    mov dword [pdpt_table + (510 * 8)], eax

    ; One temporary low 2-MiB page exists only for the mode transition.
    mov dword [low_page_directory], PAGE_2M_FLAGS

    mov eax, high_page_table
    or eax, PAGE_PRESENT_WRITE
    mov dword [high_page_directory], eax

    ; The final alias maps only linked pages and never grants W+X.
    mov esi, _text_start
    mov edi, _text_end
    mov ebx, PAGE_PRESENT
    xor ecx, ecx
    call map_high_pages32

    mov esi, _rodata_start
    mov edi, _rodata_end
    mov ebx, PAGE_PRESENT
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    mov esi, _data_start
    mov edi, _data_end
    mov ebx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    mov esi, _bss_start
    mov edi, _bss_end
    mov ebx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    mov esi, _c_core_bridge_start
    mov edi, _c_core_bridge_end
    mov ebx, PAGE_PRESENT
    xor ecx, ecx
    call map_high_pages32

    mov esi, _c_core_text_start
    mov edi, _c_core_text_end
    mov ebx, PAGE_PRESENT
    xor ecx, ecx
    call map_high_pages32

    mov esi, _c_core_rodata_start
    mov edi, _c_core_rodata_end
    mov ebx, PAGE_PRESENT
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    mov esi, _c_core_data_start
    mov edi, _c_core_data_end
    mov ebx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    mov esi, _c_core_bss_start
    mov edi, _c_core_bss_end
    mov ebx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call map_high_pages32

    lgdt [gdt64_pointer]

    mov eax, cr4
    or eax, CR4_PAE_BIT
    mov cr4, eax

    mov eax, pml4_table
    mov cr3, eax

    mov ecx, EFER_MSR
    rdmsr
    or eax, EFER_LME_BIT | EFER_NXE_BIT
    wrmsr

    mov eax, cr0
    or eax, CR0_PG_BIT | CR0_WP_BIT
    mov cr0, eax

    jmp 0x08:long_mode_entry

.unsupported:
    call serial_init32
    mov esi, unsupported_message
    call serial_write32
    jmp halt32

.memory_map_error:
    call serial_init32
    mov esi, memory_map_error_message
    call serial_write32
    jmp halt32

map_high_pages32:
    cmp esi, edi
    jae .done
.next:
    mov eax, esi
    shr eax, 12
    and eax, 0x1FF
    shl eax, 3
    add eax, high_page_table
    mov edx, esi
    and edx, 0xFFFFF000
    or edx, ebx
    mov dword [eax], edx
    mov dword [eax + 4], ecx
    add esi, 4096
    cmp esi, edi
    jb .next
.done:
    ret

cpu_has_long_mode:
    ; First prove CPUID itself by toggling the architectural EFLAGS.ID bit.
    pushfd
    pop eax
    mov ecx, eax
    xor eax, CPUID_ID_BIT
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, CPUID_ID_BIT
    jz .missing

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .missing
    mov eax, 0x80000001
    cpuid
    test edx, CPUID_LONG_MODE_BIT
    jz .missing
    test edx, CPUID_NX_BIT
    jz .missing
    mov eax, 1
    ret
.missing:
    xor eax, eax
    ret

serial_init32:
    mov dx, COM1_DATA + 1
    xor al, al
    out dx, al
    mov dx, COM1_DATA + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1_DATA
    mov al, 0x01
    out dx, al
    mov dx, COM1_DATA + 1
    xor al, al
    out dx, al
    mov dx, COM1_DATA + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1_DATA + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1_DATA + 4
    mov al, 0x0B
    out dx, al
    ret

serial_write32:
    lodsb
    test al, al
    jz .done
    call serial_putc32
    test eax, eax
    jz halt32
    jmp serial_write32
.done:
    ret

serial_putc32:
    mov bl, al
    mov dx, COM1_LSR
    mov ecx, SERIAL_TX_POLLS
.poll:
    in al, dx
    test al, 0x20
    jnz .ready
    loop .poll
    xor eax, eax
    ret
.ready:
    mov al, bl
    mov dx, COM1_DATA
    out dx, al
    mov eax, 1
    ret

halt32:
    cli
.loop:
    hlt
    jmp .loop

BITS 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov esp, bootstrap_stack_top
    xor rbp, rbp

    ; LMA is read-only proof that the far transfer really entered IA-32e.
    mov rax, cr0
    test eax, CR0_PG_BIT
    jz long_mode_state_error
    test eax, CR0_WP_BIT
    jz long_mode_state_error
    mov rax, cr4
    test eax, CR4_PAE_BIT
    jz long_mode_state_error
    mov ecx, EFER_MSR
    rdmsr
    test eax, EFER_LMA_BIT
    jz long_mode_state_error
    test eax, EFER_NXE_BIT
    jz long_mode_state_error

    call serial_init64
    lea rsi, [rel success_message]
    call serial_write64

    mov eax, higher_half_entry
    mov rdx, HIGHER_HALF_BASE
    add rax, rdx
    jmp rax

higher_half_entry:
    ; From here RIP-relative references resolve through the canonical alias.
    lea rsp, [rel bootstrap_stack_top]
    xor rbp, rbp

    ; Revoke the temporary low mapping only after both RIP and RSP are high,
    ; then reload CR3 to invalidate all stale low translations.
    mov qword [rel pml4_table], 0
    mov eax, pml4_table
    mov cr3, rax

.higher_rip:
    lea rax, [rel .higher_rip]
    mov rdx, HIGHER_HALF_BASE
    cmp rax, rdx
    jb higher_half_state_error
    mov rax, rsp
    cmp rax, rdx
    jb higher_half_state_error
    cmp qword [rel pml4_table], 0
    jne higher_half_state_error

    mov rax, cr0
    test eax, CR0_WP_BIT
    jz higher_half_state_error
    mov ecx, EFER_MSR
    rdmsr
    test eax, EFER_NXE_BIT
    jz higher_half_state_error

    mov esi, higher_half_entry
    mov edx, PAGE_PRESENT
    xor ecx, ecx
    call verify_high_page64
    mov esi, success_message
    mov edx, PAGE_PRESENT
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64
    mov esi, x86_64_nx_probe_target
    mov edx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64
    mov esi, pml4_table
    mov edx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64

    lea rsi, [rel higher_half_paging_message]
    call serial_write64

    call x86_64_exception_init
x86_64_ud2_probe:
    ud2
x86_64_ud2_resume:
    lea rax, [rel x86_64_nx_probe_target]
    jmp rax
x86_64_nx_resume:
    call x86_64_physical_memory_selftest64
    test eax, eax
    jz physical_memory_state_error
    call physical_frame_test_high_window64
    test eax, eax
    jz elf64_loader_state_error
    call x86_64_elf64_loader_selftest64
    test eax, eax
    jz .high_loader_fail
    call physical_frame_test_window_clear64
    test eax, eax
    jz elf64_loader_state_error
    call physical_frame_test_high_window64
    test eax, eax
    jz user_execution_state_error
    call x86_64_user_execution_selftest64
    test eax, eax
    jz user_execution_state_error
    call x86_64_user_execution_table_metadata_clear64
    test eax, eax
    jz user_execution_state_error
    lea rsi, [rel early_execution_tables_message]
    call serial_write64
    call physical_frame_test_window_is_clear64
    test eax, eax
    jz user_execution_state_error
    lea rsi, [rel high_frame_consumers_message]
    call serial_write64
    call x86_64_process_scheduler_selftest64
    test eax, eax
    jz process_scheduler_state_error
    jmp .high_loader_done

.high_loader_fail:
    call physical_frame_test_window_clear64
    jmp elf64_loader_state_error
.high_loader_done:
    call x86_64_timer_interrupt_selftest64
    test eax, eax
    jz timer_interrupt_state_error
    call x86_64_process_preemption_selftest64
    test eax, eax
    jz process_preemption_state_error
    call x86_64_process_quantum_selftest64
    test eax, eax
    jz quantum_switch_state_error
    call x86_64_process_runqueue_selftest64
    test eax, eax
    jz runqueue_lifecycle_state_error
    call x86_64_process_deadline_sleep_selftest64
    test eax, eax
    jz deadline_sleep_state_error
    call x86_64_process_spawn_wait_selftest64
    test eax, eax
    jz spawn_wait_state_error
    lea rsi, [rel exception_recovery_message]
    call serial_write64
    call x86_64_c_core_handoff64
    test eax, eax
    jz c_core_handoff_state_error
    lea rsi, [rel c_core_handoff_message]
    call serial_write64
    call x86_64_c_control_handoff64
    test eax, eax
    jz c_kernel_control_state_error
    call x86_64_process_table_metadata_clear64
    test eax, eax
    jz c_kernel_control_state_error
    lea rsi, [rel dynamic_process_tables_message]
    call serial_write64
    lea rsi, [rel ring3_shell_message]
    call serial_write64
    jmp halt64

x86_64_c_core_handoff64:
    pushfq
    pop rax
    test eax, (1 << 9)
    jnz .fail
    mov rax, cr3
    mov edx, pml4_table
    cmp rax, rdx
    jne .fail

    mov esi, _c_core_bridge_start
    mov edx, PAGE_PRESENT
    xor ecx, ecx
    call verify_high_page64
    mov esi, _c_core_text_start
    mov edx, PAGE_PRESENT
    xor ecx, ecx
    call verify_high_page64
    mov esi, _c_core_rodata_start
    mov edx, PAGE_PRESENT
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64
    mov esi, _c_core_data_start
    mov edx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64
    mov esi, _c_core_bss_start
    mov edx, PAGE_PRESENT_WRITE
    mov ecx, PAGE_NX_HIGH
    call verify_high_page64

    lea rdi, [rel x86_64_c_handoff]
    mov r8, rdi
    xor eax, eax
    mov ecx, C_HANDOFF_QWORDS
    rep stosq

    mov dword [r8 + C_H_VERSION], C_HANDOFF_VERSION
    mov dword [r8 + C_H_SIZE], C_HANDOFF_SIZE
    mov qword [r8 + C_H_FLAGS], C_HANDOFF_FLAGS
    mov rax, HIGHER_HALF_BASE
    mov qword [r8 + C_H_HIGHER_HALF], rax
    mov rax, cr3
    mov qword [r8 + C_H_CR3], rax
    lea rax, [rel pml4_table]
    mov qword [r8 + C_H_PML4], rax
    mov rax, 0xFFFF800000000000
    mov qword [r8 + C_H_DIRECT_MAP], rax
    mov qword [r8 + C_H_MANAGED_LIMIT], 0x08000000
    mov qword [r8 + C_H_ELF_BASE], 0x00400000
    mov qword [r8 + C_H_ELF_LIMIT], 0x00408000
    lea rax, [rel _text_start]
    mov qword [r8 + C_H_TEXT_START], rax
    lea rax, [rel _text_end]
    mov qword [r8 + C_H_TEXT_END], rax
    mov qword [r8 + C_H_LIFECYCLE], C_LIFECYCLE_CAPS
    mov dword [r8 + C_H_TASK_CAPACITY], C_FIXED_CAPACITY
    mov dword [r8 + C_H_RUNQUEUE_CAP], C_FIXED_CAPACITY
    mov dword [r8 + C_H_DEADLINE_CAP], C_FIXED_CAPACITY
    mov dword [r8 + C_H_SYSCALL_ABI], C_SYSCALL_ABI

    mov r15, rsp
    lea rsp, [rel c_core_stack_top]
    and rsp, -16
    mov rdi, r8
    call x86_64_c_core_entry
    mov r14d, eax
    mov rsp, r15
    cmp r14d, 1
    jne .fail

    lea rsi, [rel x86_64_c_handoff]
    mov ecx, C_HANDOFF_QWORDS
.verify_handoff_zero:
    lodsq
    test rax, rax
    jnz .fail
    loop .verify_handoff_zero
    lea rsi, [rel x86_64_c_data_state]
    mov ecx, C_STATE_QWORDS
.verify_data_zero:
    lodsq
    test rax, rax
    jnz .fail
    loop .verify_data_zero
    lea rsi, [rel x86_64_c_bss_state]
    mov ecx, C_STATE_QWORDS
.verify_bss_zero:
    lodsq
    test rax, rax
    jnz .fail
    loop .verify_bss_zero
    mov eax, 1
    ret
.fail:
    lea rdi, [rel x86_64_c_handoff]
    xor eax, eax
    mov ecx, C_HANDOFF_QWORDS
    rep stosq
    lea rdi, [rel x86_64_c_data_state]
    mov ecx, C_STATE_QWORDS
    rep stosq
    lea rdi, [rel x86_64_c_bss_state]
    mov ecx, C_STATE_QWORDS
    rep stosq
    xor eax, eax
    ret

x86_64_c_control_handoff64:
    pushfq
    pop rax
    test eax, (1 << 9)
    jnz .fail
    mov rax, cr3
    mov edx, pml4_table
    cmp rax, rdx
    jne .fail
    cmp byte [rel c_control_active], 0
    jne .fail

    cld
    lea rdi, [rel x86_64_c_control_handoff]
    mov r8, rdi
    xor eax, eax
    mov ecx, C_CONTROL_QWORDS
    rep stosq
    mov dword [r8 + C_C_VERSION], C_CONTROL_VERSION
    mov dword [r8 + C_C_SIZE], C_CONTROL_SIZE
    mov qword [r8 + C_C_FLAGS], C_CONTROL_FLAGS
    mov qword [r8 + C_C_GENERATION], C_CONTROL_GENERATION
    mov qword [r8 + C_C_SERVICE], C_CONTROL_SERVICE
    mov dword [r8 + C_C_TASK_CAPACITY], C_FIXED_CAPACITY
    mov dword [r8 + C_C_RUNQUEUE_CAP], C_FIXED_CAPACITY
    mov dword [r8 + C_C_SYSCALL_ABI], C_SYSCALL_ABI

    mov r15, rsp
    lea rsp, [rel c_core_stack_top]
    and rsp, -16
    mov rdi, r8
    call x86_64_c_core_entry
    mov r14d, eax
    mov rsp, r15
    cmp r14d, 1
    jne .fail

    lea rsi, [rel x86_64_c_control_handoff]
    mov ecx, C_CONTROL_QWORDS
.verify_control_zero:
    lodsq
    test rax, rax
    jnz .fail
    loop .verify_control_zero
    cmp byte [rel c_control_active], 0
    jne .fail
    mov rax, cr3
    mov edx, pml4_table
    cmp rax, rdx
    jne .fail
    pushfq
    pop rax
    test eax, (1 << 9)
    jnz .fail
    mov eax, 1
    ret
.fail:
    cld
    lea rdi, [rel x86_64_c_control_handoff]
    xor eax, eax
    mov ecx, C_CONTROL_QWORDS
    rep stosq
    mov byte [rel c_control_active], 0
    xor eax, eax
    ret

verify_high_page64:
    mov eax, esi
    shr eax, 12
    and eax, 0x1FF
    lea rdi, [rel high_page_table]
    mov ebx, esi
    and ebx, 0xFFFFF000
    or ebx, edx
    mov r8d, dword [rdi + rax * 8]
    and r8d, ~PAGE_HW_AD_MASK
    cmp r8d, ebx
    jne higher_half_state_error
    cmp dword [rdi + rax * 8 + 4], ecx
    jne higher_half_state_error
    ret

long_mode_state_error:
    call serial_init64
    lea rsi, [rel state_error_message]
    call serial_write64
    jmp halt64

higher_half_state_error:
    call serial_init64
    lea rsi, [rel higher_half_state_error_message]
    call serial_write64
    jmp halt64

physical_memory_state_error:
    call serial_init64
    lea rsi, [rel physical_memory_state_error_message]
    call serial_write64
    jmp halt64

elf64_loader_state_error:
    call serial_init64
    lea rsi, [rel elf64_loader_state_error_message]
    call serial_write64
    jmp halt64

user_execution_state_error:
    call serial_init64
    lea rsi, [rel user_execution_state_error_message]
    call serial_write64
    jmp halt64

process_scheduler_state_error:
    call serial_init64
    lea rsi, [rel process_scheduler_state_error_message]
    call serial_write64
    jmp halt64

timer_interrupt_state_error:
    call serial_init64
    lea rsi, [rel timer_interrupt_state_error_message]
    call serial_write64
    jmp halt64

process_preemption_state_error:
    call serial_init64
    lea rsi, [rel process_preemption_state_error_message]
    call serial_write64
    jmp halt64

quantum_switch_state_error:
    call serial_init64
    lea rsi, [rel quantum_switch_state_error_message]
    call serial_write64
    jmp halt64

runqueue_lifecycle_state_error:
    call serial_init64
    lea rsi, [rel runqueue_lifecycle_state_error_message]
    call serial_write64
    jmp halt64

deadline_sleep_state_error:
    call serial_init64
    lea rsi, [rel deadline_sleep_state_error_message]
    call serial_write64
    jmp halt64

spawn_wait_state_error:
    call serial_init64
    lea rsi, [rel spawn_wait_state_error_message]
    call serial_write64
    jmp halt64

c_core_handoff_state_error:
    call serial_init64
    lea rsi, [rel c_core_handoff_state_error_message]
    call serial_write64
    jmp halt64

ring3_shell_state_error:
    call serial_init64
    lea rsi, [rel ring3_shell_state_error_message]
    call serial_write64
    jmp halt64

c_kernel_control_state_error:
    call serial_init64
    lea rsi, [rel c_kernel_control_state_error_message]
    call serial_write64
    jmp halt64

section .c_core_bridge
x86_64_c_serial_write64:
    test rdi, rdi
    jz .callback_fail
    test rsi, rsi
    jz .callback_fail
    cmp rsi, 64
    ja .callback_fail
    lea rax, [rel _c_core_rodata_start]
    cmp rdi, rax
    jb .callback_fail
    mov rax, rdi
    add rax, rsi
    jc .callback_fail
    lea rdx, [rel _c_core_rodata_end]
    cmp rax, rdx
    ja .callback_fail

    push rbx
    push r12
    push r13
    mov r12, rdi
    mov r13, rsi
.callback_loop:
    mov al, byte [r12]
    call serial_putc64
    test eax, eax
    jz .callback_send_fail
    inc r12
    dec r13
    jnz .callback_loop
    pop r13
    pop r12
    pop rbx
    mov eax, 1
    ret
.callback_send_fail:
    pop r13
    pop r12
    pop rbx
.callback_fail:
    xor eax, eax
    ret

align 256
x86_64_c_process_shell64:
    cmp rdi, C_CONTROL_GENERATION
    jne .control_fail
    pushfq
    pop rax
    test eax, (1 << 9)
    jnz .control_fail
    mov rax, cr3
    mov edx, pml4_table
    cmp rax, rdx
    jne .control_fail
    cmp byte [rel c_control_active], 0
    jne .control_fail
    mov byte [rel c_control_active], 1

    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8
    call x86_64_process_shell64
    add rsp, 8
    mov r10d, eax
    mov rax, cr3
    mov edx, pml4_table
    cmp rax, rdx
    jne .control_return_invalid
    pushfq
    pop rax
    test eax, (1 << 9)
    jz .control_restore
.control_return_invalid:
    xor r10d, r10d
.control_restore:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    mov byte [rel c_control_active], 0
    mov eax, r10d
    ret
.control_fail:
    xor eax, eax
    ret

section .text
serial_init64:
    mov dx, COM1_DATA + 1
    xor al, al
    out dx, al
    mov dx, COM1_DATA + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1_DATA
    mov al, 0x01
    out dx, al
    mov dx, COM1_DATA + 1
    xor al, al
    out dx, al
    mov dx, COM1_DATA + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1_DATA + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1_DATA + 4
    mov al, 0x0B
    out dx, al
    ret

serial_write64:
    lodsb
    test al, al
    jz .done
    call serial_putc64
    test eax, eax
    jz halt64
    jmp serial_write64
.done:
    ret

serial_putc64:
    mov bl, al
    mov dx, COM1_LSR
    mov ecx, SERIAL_TX_POLLS
.poll:
    in al, dx
    test al, 0x20
    jnz .ready
    loop .poll
    xor eax, eax
    ret
.ready:
    mov al, bl
    mov dx, COM1_DATA
    out dx, al
    mov eax, 1
    ret

halt64:
    cli
.loop:
    hlt
    jmp .loop

section .rodata
early_execution_tables_message db "REIST_X86_64_EARLY_EXECUTION_TABLES_OK", 13, 10, 0
dynamic_process_tables_message db "REIST_X86_64_DYNAMIC_PROCESS_TABLES_OK", 13, 10, 0
high_frame_consumers_message db "REIST_X86_64_HIGH_FRAME_CONSUMERS_OK", 13, 10, 0
unsupported_message db "REIST_X86_64_UNSUPPORTED", 13, 10, 0
memory_map_error_message db "REIST_X86_64_MEMORY_MAP_ERROR", 13, 10, 0
state_error_message db "REIST_X86_64_LONG_MODE_STATE_ERROR", 13, 10, 0
higher_half_state_error_message db "REIST_X86_64_HIGHER_HALF_STATE_ERROR", 13, 10, 0
physical_memory_state_error_message db "REIST_X86_64_PHYSICAL_MEMORY_ERROR", 13, 10, 0
elf64_loader_state_error_message db "REIST_X86_64_ELF64_LOAD_ERROR", 13, 10, 0
user_execution_state_error_message db "REIST_X86_64_USER_EXECUTION_ERROR", 13, 10, 0
process_scheduler_state_error_message db "REIST_X86_64_PROCESS_SCHEDULER_ERROR", 13, 10, 0
timer_interrupt_state_error_message db "REIST_X86_64_TIMER_IRQ_ERROR", 13, 10, 0
process_preemption_state_error_message db "REIST_X86_64_TIMER_PREEMPTION_ERROR", 13, 10, 0
quantum_switch_state_error_message db "REIST_X86_64_QUANTUM_SWITCH_ERROR", 13, 10, 0
runqueue_lifecycle_state_error_message db "REIST_X86_64_RUNQUEUE_LIFECYCLE_ERROR", 13, 10, 0
deadline_sleep_state_error_message db "REIST_X86_64_DEADLINE_SLEEP_ERROR", 13, 10, 0
spawn_wait_state_error_message db "REIST_X86_64_SPAWN_WAIT_ERROR", 13, 10, 0
c_core_handoff_state_error_message db "REIST_X86_64_C_CORE_HANDOFF_ERROR", 13, 10, 0
c_kernel_control_state_error_message db "REIST_X86_64_C_KERNEL_CONTROL_ERROR", 13, 10, 0
ring3_shell_state_error_message db "REIST_X86_64_RING3_SHELL_ERROR", 13, 10, 0
success_message db "REIST_X86_64_LONG_MODE_BOOT_OK", 13, 10, 0
higher_half_paging_message db "REIST_X86_64_HIGHER_HALF_PAGING_OK", 13, 10, 0
exception_recovery_message db "REIST_X86_64_EXCEPTION_RECOVERY_OK", 13, 10, 0
c_core_handoff_message db "REIST_X86_64_C_CORE_HANDOFF_OK", 13, 10, 0
ring3_shell_message db "REIST_X86_64_RING3_SHELL_OK", 13, 10, 0

align 8
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt64_end:

gdt64_pointer:
    dw gdt64_end - gdt64 - 1
    dd gdt64

section .data
align 8
boot_magic_value:
    dd 0
boot_info_value:
    dd 0
x86_64_nx_probe_target:
    db 0xC3

section .bss
alignb 4096
pml4_table:
    resb 4096
alignb 4096
pdpt_table:
    resb 4096
alignb 4096
low_page_directory:
    resb 4096
alignb 4096
high_page_directory:
    resb 4096
alignb 4096
high_page_table:
    resb 4096
alignb 4096
bootstrap_stack_bottom:
    resb 16384
bootstrap_stack_top:
alignb 16
c_core_stack_bottom:
    resb 16384
c_core_stack_top:
alignb 8
c_control_active:
    resq 1

section .c_core_text progbits alloc exec nowrite align=16
global x86_64_c_core_entry
x86_64_c_core_entry:
    incbin C_CORE_TEXT_PATH

section .c_core_rodata progbits alloc noexec nowrite align=16
    incbin C_CORE_RODATA_PATH

section .c_core_data progbits alloc noexec write align=16
global x86_64_c_data_state
x86_64_c_data_state:
    incbin C_CORE_DATA_PATH

section .c_core_bss nobits alloc noexec write align=16
global x86_64_c_bss_state
x86_64_c_bss_state:
    resb 32
alignb 16
x86_64_c_handoff:
    resb C_HANDOFF_SIZE
alignb 16
x86_64_c_control_handoff:
    resb C_CONTROL_SIZE
