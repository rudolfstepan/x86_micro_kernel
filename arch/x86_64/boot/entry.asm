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
extern x86_64_exception_init
extern x86_64_physical_memory_init32
extern x86_64_physical_memory_selftest64
extern x86_64_elf64_loader_selftest64
extern x86_64_user_execution_selftest64
extern x86_64_process_scheduler_selftest64
extern x86_64_timer_interrupt_selftest64
extern x86_64_process_preemption_selftest64
extern x86_64_process_quantum_selftest64
extern _text_start
extern _text_end
extern _rodata_start
extern _rodata_end
extern _data_start
extern _data_end
extern _bss_start
extern _bss_end

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
    call x86_64_elf64_loader_selftest64
    test eax, eax
    jz elf64_loader_state_error
    call x86_64_user_execution_selftest64
    test eax, eax
    jz user_execution_state_error
    call x86_64_process_scheduler_selftest64
    test eax, eax
    jz process_scheduler_state_error
    call x86_64_timer_interrupt_selftest64
    test eax, eax
    jz timer_interrupt_state_error
    call x86_64_process_preemption_selftest64
    test eax, eax
    jz process_preemption_state_error
    call x86_64_process_quantum_selftest64
    test eax, eax
    jz quantum_switch_state_error
    lea rsi, [rel exception_recovery_message]
    call serial_write64
    jmp halt64

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
success_message db "REIST_X86_64_LONG_MODE_BOOT_OK", 13, 10, 0
higher_half_paging_message db "REIST_X86_64_HIGHER_HALF_PAGING_OK", 13, 10, 0
exception_recovery_message db "REIST_X86_64_EXCEPTION_RECOVERY_OK", 13, 10, 0

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
