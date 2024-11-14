[BITS 32]
global start
global interrupt

extern stack_start
extern kernel_main

start:
    ; ; print `OK` to screen
    ;mov dword [0xb8000], 0x2f4b2f4f
    ; hlt
    cli                     ; Clear interrupts
    ; Initialize stack pointer to the top of the 8KB reserved stack area
    mov esp, stack_start + 8192

    push ebx                ; Push the Multiboot2 information pointer (from GRUB)
    push dword 0x36d76289  ; Push the Multiboot2 magic number for verification
    call kernel_main        ; Call the C kernel main function
    hlt                     ; Halt the CPU when kernel_main returns

section .multiboot
align 4              ; Align the section to an 8-byte boundary
header_start:
    ; dd 0xe85250d6            ; Multiboot2 magic number
    ; dd 0                      ; Architecture (0 for i386)
    ; dd header_end - header_start ; Header length
    ; ; checksum
    ; dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))
    dd 0x1BADB002             ; Magic number
    dd 0x00                   ; Flags
    dd -(0x1BADB002 + 0x00)   ; Checksum

    ; Information request tag
    align 8
    dw 1                      ; Tag type: Information request
    dw 0                      ; Flags
    dd 16                     ; Size of this tag
    dd 6                      ; Request BIOS memory map
    dd 5                      ; Request BIOS boot device

    ; Framebuffer tag (optional, for setting video mode)
    align 8
    dw 4                      ; Tag type: Framebuffer
    dw 0                      ; Flags
    dd 24                     ; Size of this tag
    dd 1024                   ; Framebuffer width
    dd 768                    ; Framebuffer height
    dd 32                     ; Bits per pixel

    ; End tag
    align 8
    dw 0                      ; Tag type: End
    dw 0                      ; Flags
    dd 8                      ; Size of this tag

header_end:

section .text
; stublet:
;     extern kernel_main
;     ; Jump to the kernel's main function
;     ; Pass the Multiboot magic number and the Multiboot information structure pointer
;     push dword 0xe85250d6 ; Multiboot magic number
;     push ebx              ; ebx contains the pointer to the Multiboot information structure
;     call main
;     jmp $
global gdt_flush     
extern gp            
gdt_flush:
    lgdt [gp]        
    mov ax, 0x10      
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:flush2   
flush2:
    ret    

global idt_load
extern idtp
idt_load:
	lidt[idtp]
	ret

; isr ...

section .text

global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

;  0: Divide By Zero Exception
isr0:
    cli
    push byte 0
    push byte 0
    jmp isr_common_stub

;  1: Debug Exception
isr1:
    cli
    push byte 0
    push byte 1
    jmp isr_common_stub

;  2: Non Maskable Interrupt Exception
isr2:
    cli
    push byte 0
    push byte 2
    jmp isr_common_stub

;  3: Int 3 Exception
isr3:
    cli
    push byte 0
    push byte 3
    jmp isr_common_stub

;  4: INTO Exception
isr4:
    cli
    push byte 0
    push byte 4
    jmp isr_common_stub

;  5: Out of Bounds Exception
isr5:
    cli
    push byte 0
    push byte 5
    jmp isr_common_stub

;  6: Invalid Opcode Exception
isr6:
    cli
    push byte 0
    push byte 6
    jmp isr_common_stub

;  7: Coprocessor Not Available Exception
isr7:
    cli
    push byte 0
    push byte 7
    jmp isr_common_stub

;  8: Double Fault Exception (With Error Code!)
isr8:
    cli
    push byte 8
    jmp isr_common_stub

;  9: Coprocessor Segment Overrun Exception
isr9:
    cli
    push byte 0
    push byte 9
    jmp isr_common_stub

; 10: Bad TSS Exception (With Error Code!)
isr10:
    cli
    push byte 10
    jmp isr_common_stub

; 11: Segment Not Present Exception (With Error Code!)
isr11:
    cli
    push byte 11
    jmp isr_common_stub

; 12: Stack Fault Exception (With Error Code!)
isr12:
    cli
    push byte 12
    jmp isr_common_stub

; 13: General Protection Fault Exception (With Error Code!)
isr13:
    cli
    push byte 13
    jmp isr_common_stub

; 14: Page Fault Exception (With Error Code!)
isr14:
    cli
    push byte 14
    jmp isr_common_stub

; 15: Reserved Exception
isr15:
    cli
    push byte 0
    push byte 15
    jmp isr_common_stub

; 16: Floating Point Exception
isr16:
    cli
    push byte 0
    push byte 16
    jmp isr_common_stub

; 17: Alignment Check Exception
isr17:
    cli
    push byte 0
    push byte 17
    jmp isr_common_stub

; 18: Machine Check Exception
isr18:
    cli
    push byte 0
    push byte 18
    jmp isr_common_stub

; 19: Reserved
isr19:
    cli
    push byte 0
    push byte 19
    jmp isr_common_stub

; 20: Reserved
isr20:
    cli
    push byte 0
    push byte 20
    jmp isr_common_stub

; 21: Reserved
isr21:
    cli
    push byte 0
    push byte 21
    jmp isr_common_stub

; 22: Reserved
isr22:
    cli
    push byte 0
    push byte 22
    jmp isr_common_stub

; 23: Reserved
isr23:
    cli
    push byte 0
    push byte 23
    jmp isr_common_stub

; 24: Reserved
isr24:
    cli
    push byte 0
    push byte 24
    jmp isr_common_stub

; 25: Reserved
isr25:
    cli
    push byte 0
    push byte 25
    jmp isr_common_stub

; 26: Reserved
isr26:
    cli
    push byte 0
    push byte 26
    jmp isr_common_stub

; 27: Reserved
isr27:
    cli
    push byte 0
    push byte 27
    jmp isr_common_stub

; 28: Reserved
isr28:
    cli
    push byte 0
    push byte 28
    jmp isr_common_stub

; 29: Reserved
isr29:
    cli
    push byte 0
    push byte 29
    jmp isr_common_stub

; 30: Reserved
isr30:
    cli
    push byte 0
    push byte 30
    jmp isr_common_stub

; 31: Reserved
isr31:
    cli
    push byte 0
    push byte 31
    jmp isr_common_stub

extern fault_handler

isr_common_stub:
	pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax
    mov eax, fault_handler
    call fault_handler
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

; 32: IRQ0
irq0:
    cli
    push byte 0
    push byte 32
    jmp irq_common_stub

; 33: IRQ1
irq1:
    cli
    push byte 0
    push byte 33
    jmp irq_common_stub

; 34: IRQ2
irq2:
    cli
    push byte 0
    push byte 34
    jmp irq_common_stub

; 35: IRQ3
irq3:
    cli
    push byte 0
    push byte 35
    jmp irq_common_stub

; 36: IRQ4
irq4:
    cli
    push byte 0
    push byte 36
    jmp irq_common_stub

; 37: IRQ5
irq5:
    cli
    push byte 0
    push byte 37
    jmp irq_common_stub

; 38: IRQ6
irq6:
    cli
    push byte 0
    push byte 38
    jmp irq_common_stub

; 39: IRQ7
irq7:
    cli
    push byte 0
    push byte 39
    jmp irq_common_stub

; 40: IRQ8
irq8:
    cli
    push byte 0
    push byte 40
    jmp irq_common_stub

; 41: IRQ9
irq9:
    cli
    push byte 0
    push byte 41
    jmp irq_common_stub

; 42: IRQ10
irq10:
    cli
    push byte 0
    push byte 42
    jmp irq_common_stub

; 43: IRQ11
irq11:
    cli
    push byte 0
    push byte 43
    jmp irq_common_stub

; 44: IRQ12
irq12:
    cli
    push byte 0
    push byte 44
    jmp irq_common_stub

; 45: IRQ13
irq13:
    cli
    push byte 0
    push byte 45
    jmp irq_common_stub

; 46: IRQ14
irq14:
    cli
    push byte 0
    push byte 46
    jmp irq_common_stub

; 47: IRQ15
irq15:
    cli
    push byte 0
    push byte 47
    jmp irq_common_stub

; 0x80: System Call
; irq_syscall:
;     cli
;     push byte 0
;     push byte 0x80
;     iret

; Assembly entry point for `int 0x80`
; [BITS 32]
; global syscall_handler_asm
; extern syscall_handler

; section .text
; syscall_handler_asm:
;     pusha                   ; Save all general-purpose registers
;     push ds                 ; Save data segment register
;     push es                 ; Save extra segment register
;     push fs                 ; Save fs segment register
;     push gs                 ; Save gs segment register


;     call syscall_handler    ; Call the external C or assembly syscall handler function

;     pop gs                  ; Restore gs segment register
;     pop fs                  ; Restore fs segment register
;     pop es                  ; Restore extra segment register
;     pop ds                  ; Restore data segment register
;     popa                    ; Restore all general-purpose registers

;     iretd                   ; Return from interrupt, restoring EFLAGS, CS, and EIP

extern irq_handler

irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp

    push eax
    mov eax, irq_handler
    call eax
    pop eax

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret



section .syscall_table
align 4  ; Align to 4 bytes for pointer alignment

; Define the syscall table entries here, or let it be initialized by the kernel
; If entries are to be defined here, list them as labels or addresses.

syscall_table_start:
;times NUM_SYSCALLS dd 0  ; Reserve space for NUM_SYSCALLS entries (adjust as necessary)
syscall_table_end:


section .bss
resb 8192                         ; Reserve 8KB for the stack
stack_start: