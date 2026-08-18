; @file arch/x86/boot/vbe_runtime.asm
; @brief Begrenzter Protected-/Real-Mode-Thunk für einen vorbereiteten VBE-Modus.

BITS 32

global vbe_runtime_set_mode
global vbe_runtime_set_text_mode

section .text

LOW_BLOB_ADDRESS       equ 0x00009000
LOW_REAL_STACK         equ 0x00007FF0
LOW_STATE_ADDRESS      equ 0x00009800
STATE_GDTR             equ LOW_STATE_ADDRESS + 0
STATE_IDTR             equ LOW_STATE_ADDRESS + 6
STATE_CR0              equ LOW_STATE_ADDRESS + 12
STATE_CR3              equ LOW_STATE_ADDRESS + 16
STATE_ESP              equ LOW_STATE_ADDRESS + 20
STATE_RESULT           equ LOW_STATE_ADDRESS + 24
STATE_MODE             equ LOW_STATE_ADDRESS + 28
STATE_OPERATION        equ LOW_STATE_ADDRESS + 32
STATE_PIC_MASTER       equ LOW_STATE_ADDRESS + 36
STATE_PIC_SLAVE        equ LOW_STATE_ADDRESS + 37
STATE_APIC_ACTIVE      equ LOW_STATE_ADDRESS + 38
STATE_APIC_LVT         equ LOW_STATE_ADDRESS + 40
TEMP_GDT_ADDRESS       equ 0x00009840
TEMP_GDTR_ADDRESS      equ 0x00009860
REAL_IDTR_ADDRESS      equ 0x00009868

CODE32_SELECTOR        equ 0x08
DATA32_SELECTOR        equ 0x10
CODE16_SELECTOR        equ 0x18
VBE_LFB_REQUEST        equ 0x4000
PIC_MASTER_MASK        equ 0x21
PIC_SLAVE_MASK         equ 0xA1
IA32_APIC_BASE_MSR     equ 0x1B
APIC_BASE_ENABLE       equ 1 << 11
APIC_STANDARD_BASE     equ 0xFEE00000
APIC_TIMER_LVT         equ APIC_STANDARD_BASE + 0x320
APIC_LVT_MASKED        equ 1 << 16

vbe_runtime_set_mode:
    movzx eax, word [esp + 4]
    xor edx, edx
    jmp vbe_runtime_transition

vbe_runtime_set_text_mode:
    xor eax, eax
    mov edx, 1

vbe_runtime_transition:
    pushfd
    cli
    push ds
    push es
    push fs
    push gs
    pushad
    mov [STATE_MODE], ax
    mov [STATE_OPERATION], edx
    mov [STATE_ESP], esp
    sgdt [STATE_GDTR]
    sidt [STATE_IDTR]
    mov eax, cr0
    mov [STATE_CR0], eax
    mov eax, cr3
    mov [STATE_CR3], eax
    mov dword [STATE_RESULT], -19

    ; Kernel IRQ vectors are invalid under the BIOS IVT.  Some physical video
    ; BIOSes execute STI internally, so quiesce the remapped PIC and periodic
    ; Local-APIC timer before entering real mode.  Restore their exact state
    ; before the caller's saved EFLAGS can enable interrupts again.
    mov byte [STATE_APIC_ACTIVE], 0
    mov dx, PIC_MASTER_MASK
    in al, dx
    mov [STATE_PIC_MASTER], al
    mov dx, PIC_SLAVE_MASK
    in al, dx
    mov [STATE_PIC_SLAVE], al
    mov al, 0xFF
    out dx, al
    mov dx, PIC_MASTER_MASK
    out dx, al

    mov ecx, IA32_APIC_BASE_MSR
    rdmsr
    test eax, APIC_BASE_ENABLE
    jz .interrupt_sources_quiesced
    test edx, edx
    jnz .interrupt_sources_quiesced
    and eax, 0xFFFFF000
    cmp eax, APIC_STANDARD_BASE
    jne .interrupt_sources_quiesced
    mov eax, [APIC_TIMER_LVT]
    mov [STATE_APIC_LVT], eax
    or eax, APIC_LVT_MASKED
    mov [APIC_TIMER_LVT], eax
    mov byte [STATE_APIC_ACTIVE], 1
.interrupt_sources_quiesced:

    cld
    mov esi, runtime_transition_gdt
    mov edi, TEMP_GDT_ADDRESS
    mov ecx, (runtime_transition_gdt_end - runtime_transition_gdt) / 4
    rep movsd
    mov word [TEMP_GDTR_ADDRESS], runtime_transition_gdt_end - runtime_transition_gdt - 1
    mov dword [TEMP_GDTR_ADDRESS + 2], TEMP_GDT_ADDRESS
    mov word [REAL_IDTR_ADDRESS], 0x03FF
    mov dword [REAL_IDTR_ADDRESS + 2], 0

    mov esi, runtime_real_blob_start
    mov edi, LOW_BLOB_ADDRESS
    mov ecx, runtime_real_blob_end - runtime_real_blob_start
    rep movsb

    lidt [REAL_IDTR_ADDRESS]
    lgdt [TEMP_GDTR_ADDRESS]
    jmp dword CODE16_SELECTOR:runtime_leave_paging

BITS 16
runtime_leave_paging:
    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax
    jmp dword CODE16_SELECTOR:runtime_leave_protected

runtime_leave_protected:
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp word 0x0000:LOW_BLOB_ADDRESS

runtime_real_blob_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, LOW_REAL_STACK
    cld

    cmp dword [STATE_OPERATION], 0
    jne .set_text_mode
    mov bx, [STATE_MODE]
    or bx, VBE_LFB_REQUEST
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .failed
    mov ax, 0x4F03
    int 0x10
    cmp ax, 0x004F
    jne .failed
    and bx, 0x3FFF
    cmp bx, [STATE_MODE]
    jne .failed
    mov dword [STATE_RESULT], 0
    jmp .return_protected

.set_text_mode:
    mov ax, 0x0003
    int 0x10
    mov ah, 0x0F
    int 0x10
    cmp al, 0x03
    jne .failed
    mov dword [STATE_RESULT], 0
    jmp .return_protected

.failed:
    mov ax, 0x0003
    int 0x10

.return_protected:
    cli
    lgdt [TEMP_GDTR_ADDRESS]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE32_SELECTOR:runtime_resume32
runtime_real_blob_end:

BITS 32
runtime_resume32:
    mov ax, DATA32_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [STATE_CR3]
    mov cr3, eax
    mov eax, [STATE_CR0]
    mov cr0, eax
    jmp dword CODE32_SELECTOR:runtime_paged32

runtime_paged32:
    lgdt [STATE_GDTR]
    lidt [STATE_IDTR]
    cmp byte [STATE_APIC_ACTIVE], 1
    jne .apic_restored
    mov eax, [STATE_APIC_LVT]
    mov [APIC_TIMER_LVT], eax
.apic_restored:
    mov dx, PIC_SLAVE_MASK
    mov al, [STATE_PIC_SLAVE]
    out dx, al
    mov dx, PIC_MASTER_MASK
    mov al, [STATE_PIC_MASTER]
    out dx, al
    mov esp, [STATE_ESP]
    popad
    pop gs
    pop fs
    pop es
    pop ds
    mov eax, [STATE_RESULT]
    popfd
    ret

align 8
runtime_transition_gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x008F9A000000FFFF
runtime_transition_gdt_end:

%if (runtime_real_blob_end - runtime_real_blob_start) > 0x700
    %error "VBE real-mode blob exceeds reserved low-memory window"
%endif
