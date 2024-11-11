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
    mov esp, stack_start    ; Set up the stack pointer
    push ebx                ; Push the Multiboot2 information pointer (from GRUB)
    push dword 0x36d76289  ; Push the Multiboot2 magic number for verification
    call kernel_main        ; Call the C kernel main function
    hlt                     ; Halt the CPU when kernel_main returns

section .multiboot_header
align 8                ; Align the section to an 8-byte boundary
header_start:
    dd 0xe85250d6            ; Multiboot2 magic number
    dd 0                      ; Architecture (0 for i386)
    dd header_end - header_start ; Header length
    ; checksum
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

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


; global irq0
; global irq1
; global irq2
; global irq3
; global irq4
; global irq5
; global irq6
; global irq7
; global irq8
; global irq9
; global irq10
; global irq11
; global irq12
; global irq13
; global irq14
; global irq15

; ; 32: IRQ0
; irq0:
;     cli
;     push byte 0
;     push byte 32
;     jmp irq_common_stub

; ; 33: IRQ1
; irq1:
;     cli
;     push byte 0
;     push byte 33
;     jmp irq_common_stub

; ; 34: IRQ2
; irq2:
;     cli
;     push byte 0
;     push byte 34
;     jmp irq_common_stub

; ; 35: IRQ3
; irq3:
;     cli
;     push byte 0
;     push byte 35
;     jmp irq_common_stub

; ; 36: IRQ4
; irq4:
;     cli
;     push byte 0
;     push byte 36
;     jmp irq_common_stub

; ; 37: IRQ5
; irq5:
;     cli
;     push byte 0
;     push byte 37
;     jmp irq_common_stub

; ; 38: IRQ6
; irq6:
;     cli
;     push byte 0
;     push byte 38
;     jmp irq_common_stub

; ; 39: IRQ7
; irq7:
;     cli
;     push byte 0
;     push byte 39
;     jmp irq_common_stub

; ; 40: IRQ8
; irq8:
;     cli
;     push byte 0
;     push byte 40
;     jmp irq_common_stub

; ; 41: IRQ9
; irq9:
;     cli
;     push byte 0
;     push byte 41
;     jmp irq_common_stub

; ; 42: IRQ10
; irq10:
;     cli
;     push byte 0
;     push byte 42
;     jmp irq_common_stub

; ; 43: IRQ11
; irq11:
;     cli
;     push byte 0
;     push byte 43
;     jmp irq_common_stub

; ; 44: IRQ12
; irq12:
;     cli
;     push byte 0
;     push byte 44
;     jmp irq_common_stub

; ; 45: IRQ13
; irq13:
;     cli
;     push byte 0
;     push byte 45
;     jmp irq_common_stub

; ; 46: IRQ14
; irq14:
;     cli
;     push byte 0
;     push byte 46
;     jmp irq_common_stub

; ; 47: IRQ15
; irq15:
;     cli
;     push byte 0
;     push byte 47
;     jmp irq_common_stub

; ; 0x80: System Call
; ; global syscall_table
; irq_syscall:
;     cli

; extern irq_handler

; irq_common_stub:
;     pusha
;     push ds
;     push es
;     push fs
;     push gs

;     mov ax, 0x10
;     mov ds, ax
;     mov es, ax
;     mov fs, ax
;     mov gs, ax
;     mov eax, esp

;     push eax
;     mov eax, irq_handler
;     call eax
;     pop eax

;     pop gs
;     pop fs
;     pop es
;     pop ds
;     popa
;     add esp, 8
;     iret

section .bss
resb 8192                         ; Reserve 8KB for the stack
stack_start: