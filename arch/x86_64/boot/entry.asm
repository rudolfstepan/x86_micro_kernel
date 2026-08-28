; Isolated REIST x86_64 transition proof.
; Multiboot v1 enters in 32-bit protected mode.  No production i386 object
; references this file.

BITS 32

MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ (1 << 0) | (1 << 1)
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
CPUID_ID_BIT        equ (1 << 21)
CPUID_LONG_MODE_BIT equ (1 << 29)
CR0_PG_BIT          equ (1 << 31)
CR4_PAE_BIT         equ (1 << 5)
EFER_MSR            equ 0xC0000080
EFER_LME_BIT        equ (1 << 8)
EFER_LMA_BIT        equ (1 << 10)
PAGE_PRESENT_WRITE  equ 0x003
PAGE_2M_FLAGS       equ 0x083
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
extern x86_64_exception_init

x86_64_bootstrap_start:
    cli
    mov esp, bootstrap_stack_top
    xor ebp, ebp

    call cpu_has_long_mode
    test eax, eax
    jz .unsupported

    ; Multiboot does not promise zeroed BSS.  Clear all three tables before
    ; publishing any hierarchy entry.
    xor eax, eax
    mov edi, pml4_table
    mov ecx, (3 * 4096) / 4
    rep stosd

    mov eax, pdpt_table
    or eax, PAGE_PRESENT_WRITE
    mov dword [pml4_table], eax

    mov eax, page_directory
    or eax, PAGE_PRESENT_WRITE
    mov dword [pdpt_table], eax

    ; Exactly one 2 MiB page identity maps the complete bootstrap.
    mov dword [page_directory], PAGE_2M_FLAGS

    lgdt [gdt64_pointer]

    mov eax, cr4
    or eax, CR4_PAE_BIT
    mov cr4, eax

    mov eax, pml4_table
    mov cr3, eax

    mov ecx, EFER_MSR
    rdmsr
    or eax, EFER_LME_BIT
    wrmsr

    mov eax, cr0
    or eax, CR0_PG_BIT
    mov cr0, eax

    jmp 0x08:long_mode_entry

.unsupported:
    call serial_init32
    mov esi, unsupported_message
    call serial_write32
    jmp halt32

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
    mov rax, cr4
    test eax, CR4_PAE_BIT
    jz long_mode_state_error
    mov ecx, EFER_MSR
    rdmsr
    test eax, EFER_LMA_BIT
    jz long_mode_state_error

    call serial_init64
    mov esi, success_message
    call serial_write64

    call x86_64_exception_init
x86_64_ud2_probe:
    ud2
x86_64_ud2_resume:
    mov esi, exception_recovery_message
    call serial_write64
    jmp halt64

long_mode_state_error:
    call serial_init64
    mov esi, state_error_message
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
state_error_message db "REIST_X86_64_LONG_MODE_STATE_ERROR", 13, 10, 0
success_message db "REIST_X86_64_LONG_MODE_BOOT_OK", 13, 10, 0
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

section .bss
align 4096
pml4_table:
    resb 4096
align 4096
pdpt_table:
    resb 4096
align 4096
page_directory:
    resb 4096
align 4096
bootstrap_stack_bottom:
    resb 16384
bootstrap_stack_top:
