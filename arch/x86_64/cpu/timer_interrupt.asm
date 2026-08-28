; Bounded legacy PIC/PIT interrupt proof for the isolated x86_64 bootstrap.

BITS 64

PIC1_COMMAND              equ 0x20
PIC1_DATA                 equ 0x21
PIC2_COMMAND              equ 0xA0
PIC2_DATA                 equ 0xA1
PIC_EOI                   equ 0x20
PIC_ICW1_INIT_ICW4        equ 0x11
PIC_MASTER_VECTOR         equ 0x20
PIC_SLAVE_VECTOR          equ 0x28
PIC_MASTER_CASCADE        equ 0x04
PIC_SLAVE_ID              equ 0x02
PIC_8086_MODE             equ 0x01
PIC_MASTER_IRQ0_ONLY      equ 0xFE
PIC_ALL_MASKED            equ 0xFF
PIT_COMMAND               equ 0x43
PIT_CHANNEL0              equ 0x40
PIT_MODE3_LOHI            equ 0x36
PIT_INPUT_HZ              equ 1193182
PIT_TARGET_HZ             equ 100
PIT_DIVISOR               equ 11932
TIMER_VECTOR              equ 32
TIMER_EXPECTED_TICKS      equ 3
TIMER_GENERATION          equ 1
TSC_DEADLINE_CYCLES       equ 3000000000
KERNEL_CODE_SELECTOR      equ 0x08
RFLAGS_IF                 equ 0x0000000000000200
HIGHER_HALF_BASE          equ 0xFFFFFFFF80000000

EXCEPTION_FRAME_VECTOR    equ (15 * 8)
EXCEPTION_FRAME_ERROR     equ (EXCEPTION_FRAME_VECTOR + 8)
EXCEPTION_FRAME_RIP       equ (EXCEPTION_FRAME_ERROR + 8)
EXCEPTION_FRAME_CS        equ (EXCEPTION_FRAME_RIP + 8)
EXCEPTION_FRAME_RFLAGS    equ (EXCEPTION_FRAME_CS + 8)

section .text
global x86_64_timer_interrupt_selftest64
global x86_64_timer_interrupt64

extern pml4_table
extern _text_start
extern _text_end
extern serial_write64

x86_64_timer_interrupt_selftest64:
    cli
    cld
    mov byte [rel timer_active], 0
    mov byte [rel timer_masks_saved], 0
    pushfq
    pop rax
    test rax, RFLAGS_IF
    jnz timer_fail
    mov rax, cr3
    mov qword [rel timer_original_cr3], rax
    mov edx, pml4_table
    cmp rax, rdx
    jne timer_fail

    mov byte [rel timer_failure_stage], 1
    mov byte [rel timer_active], 0
    mov dword [rel timer_generation], 0
    mov dword [rel timer_ticks], 0
    mov dword [rel timer_eoi_count], 0
    mov qword [rel timer_deadline], 0

    in al, PIC1_DATA
    mov byte [rel timer_saved_master_mask], al
    in al, PIC2_DATA
    mov byte [rel timer_saved_slave_mask], al
    mov byte [rel timer_masks_saved], 1

    mov byte [rel timer_failure_stage], 2
    mov al, PIC_ALL_MASKED
    out PIC1_DATA, al
    out PIC2_DATA, al
    mov al, PIC_ICW1_INIT_ICW4
    out PIC1_COMMAND, al
    call timer_io_wait64
    out PIC2_COMMAND, al
    call timer_io_wait64
    mov al, PIC_MASTER_VECTOR
    out PIC1_DATA, al
    call timer_io_wait64
    mov al, PIC_SLAVE_VECTOR
    out PIC2_DATA, al
    call timer_io_wait64
    mov al, PIC_MASTER_CASCADE
    out PIC1_DATA, al
    call timer_io_wait64
    mov al, PIC_SLAVE_ID
    out PIC2_DATA, al
    call timer_io_wait64
    mov al, PIC_8086_MODE
    out PIC1_DATA, al
    call timer_io_wait64
    out PIC2_DATA, al
    call timer_io_wait64

    mov al, PIC_MASTER_IRQ0_ONLY
    out PIC1_DATA, al
    mov al, PIC_ALL_MASKED
    out PIC2_DATA, al
    in al, PIC1_DATA
    cmp al, PIC_MASTER_IRQ0_ONLY
    jne timer_fail
    in al, PIC2_DATA
    cmp al, PIC_ALL_MASKED
    jne timer_fail

    mov byte [rel timer_failure_stage], 3
    mov al, PIT_MODE3_LOHI
    out PIT_COMMAND, al
    mov ax, PIT_DIVISOR
    out PIT_CHANNEL0, al
    mov al, ah
    out PIT_CHANNEL0, al

    rdtsc
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    mov rdx, TSC_DEADLINE_CYCLES
    add rax, rdx
    jc timer_fail
    mov qword [rel timer_deadline], rax
    mov dword [rel timer_generation], TIMER_GENERATION
    mov byte [rel timer_active], 1

    mov byte [rel timer_failure_stage], 4
    sti
.wait:
    pause
    cmp dword [rel timer_ticks], TIMER_EXPECTED_TICKS
    je .ticks_complete
    rdtsc
    shl rdx, 32
    mov eax, eax
    or rax, rdx
    cmp rax, qword [rel timer_deadline]
    jb .wait
    cli
    jmp timer_fail

.ticks_complete:
    cli
    cmp dword [rel timer_eoi_count], TIMER_EXPECTED_TICKS
    jne timer_fail
    call timer_cleanup64
    test eax, eax
    jz timer_fail_return
    lea rsi, [rel timer_ok_message]
    call serial_write64
    mov eax, 1
    ret

x86_64_timer_interrupt64:
    cmp byte [rel timer_active], 1
    jne .invalid
    cmp dword [rel timer_generation], TIMER_GENERATION
    jne .invalid
    mov rax, cr3
    cmp rax, qword [rel timer_original_cr3]
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_VECTOR], TIMER_VECTOR
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_ERROR], 0
    jne .invalid
    cmp qword [rdi + EXCEPTION_FRAME_CS], KERNEL_CODE_SELECTOR
    jne .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RFLAGS]
    test rax, RFLAGS_IF
    jz .invalid
    mov rax, qword [rdi + EXCEPTION_FRAME_RIP]
    mov edx, _text_start
    mov rcx, HIGHER_HALF_BASE
    add rdx, rcx
    cmp rax, rdx
    jb .invalid
    mov edx, _text_end
    add rdx, rcx
    cmp rax, rdx
    jae .invalid
    cmp dword [rel timer_ticks], TIMER_EXPECTED_TICKS
    jae .invalid
    inc dword [rel timer_ticks]
    inc dword [rel timer_eoi_count]
    cmp dword [rel timer_ticks], TIMER_EXPECTED_TICKS
    jne .acknowledge
    mov al, PIC_ALL_MASKED
    out PIC1_DATA, al
.acknowledge:
    mov al, PIC_EOI
    out PIC1_COMMAND, al
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

timer_cleanup64:
    cli
    mov byte [rel timer_active], 0
    mov dword [rel timer_generation], 0
    mov qword [rel timer_deadline], 0
    cmp byte [rel timer_masks_saved], 1
    jne .state_only
    mov al, PIC_ALL_MASKED
    out PIC1_DATA, al
    mov al, byte [rel timer_saved_slave_mask]
    out PIC2_DATA, al
    mov al, byte [rel timer_saved_master_mask]
    out PIC1_DATA, al
    in al, PIC1_DATA
    cmp al, byte [rel timer_saved_master_mask]
    jne .fail
    in al, PIC2_DATA
    cmp al, byte [rel timer_saved_slave_mask]
    jne .fail
.state_only:
    mov byte [rel timer_masks_saved], 0
    pushfq
    pop rax
    test rax, RFLAGS_IF
    jnz .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

timer_io_wait64:
    push rax
    mov al, 0
    out 0x80, al
    pop rax
    ret

timer_fail:
    cli
    call timer_cleanup64
timer_fail_return:
    lea rsi, [rel timer_stage_message]
    call serial_write64
    mov byte [rel timer_active], 0
    mov dword [rel timer_generation], 0
    xor eax, eax
    ret

section .rodata
timer_ok_message db "REIST_X86_64_TIMER_IRQ_OK", 13, 10, 0
timer_stage_message db "REIST_X86_64_TIMER_IRQ_ERROR", 13, 10, 0

section .bss
alignb 8
timer_original_cr3: resq 1
timer_deadline: resq 1
timer_ticks: resd 1
timer_eoi_count: resd 1
timer_generation: resd 1
timer_active: resb 1
timer_masks_saved: resb 1
timer_saved_master_mask: resb 1
timer_saved_slave_mask: resb 1
timer_failure_stage: resb 1
