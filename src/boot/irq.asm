section .text
global trigger_interrupt

trigger_interrupt:
    ; Load the address of the Registers struct from the argument
    ; mov esi, [esp + 4]     ; ESI points to the struct passed as argument

    ; ; Load register values from the struct into CPU registers
    ; mov ax, [esi]          ; Load AX
    ; mov bx, [esi + 2]      ; Load BX
    ; mov cx, [esi + 4]      ; Load CX
    ; mov dx, [esi + 6]      ; Load DX
    ; mov si, [esi + 8]      ; Load SI
    ; mov di, [esi + 10]     ; Load DI
    ; mov es, [esi + 12]     ; Load ES
    ; mov ds, [esi + 14]     ; Load DS
    ; mov al, [esi + 16]     ; Load the interrupt number into AL

    ; Jump table for known interrupts
    ; cmp al, 0x10
    ; je irq10
    ;je int_10
;     cmp al, 0x13
;     je int_13
;     cmp al, 0x15
;     je int_15
;     cmp al, 0x16
;     je int_16
;     cmp al, 0x1A
;     je int_1A
;     cmp al, 0x1C
;     je int_1C
;     ; Add more cases as needed
;     jmp unknown_interrupt
    jmp irq_common_stub


; int_10:
;     int 0x10              ; Video services
;     ret

; int_13:
;     int 0x13              ; Disk services
;     ret

; int_15:
;     int 0x15              ; System services (e.g., memory configuration)
;     ret

; int_16:
;     int 0x16              ; Keyboard services
;     ret

; int_1A:
;     int 0x1A              ; Real-time clock services
;     ret

; int_1C:
;     int 0x1C              ; User timer tick
;     ret

; unknown_interrupt:
;     ; Handle unknown or unsupported interrupts
;     ret



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
; global syscall_table
irq_syscall:
    cli

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