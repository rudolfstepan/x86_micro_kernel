global trigger_interrupt

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

trigger_interrupt:
    ; Load the address of the Registers2 struct from the stack argument
    mov esi, [esp + 4]      ; ESI points to the struct passed as an argument

    ; Load the 'int_no' field from the struct into a register
    mov eax, [esi]          ; Load the 'int_no' field into EAX (only one field in the struct)

    ; Do something with 'int_no' (e.g., print it, use it in logic, etc.)
    ; Example: Move 'int_no' to AL for further processing
    mov al, byte [esi]      ; Load the least significant byte of 'int_no' into AL

    ; Rest of your logic here...

    ret                     ; Return from the function

;     ; jmp irq_syscall
; trigger_interrupt:
;     ; Load the address of the Registers struct from the stack argument
;     mov esi, [esp + 4]      ; ESI points to the struct passed as an argument

;     ; Load register values from the struct into CPU registers
;     mov eax, [esi + 28]     ; Load EAX
;     mov ebx, [esi + 24]     ; Load EBX
;     mov ecx, [esi + 20]     ; Load ECX
;     mov edx, [esi + 16]     ; Load EDX
;     mov esi, [esi + 12]     ; Load ESI
;     mov edi, [esi + 8]      ; Load EDI
;     mov ebp, [esi + 36]     ; Load EBP (stack frame pointer)

;     ; For segment registers, ensure they are loaded properly
;     mov ax, [esi]           ; Load GS (offset 0)
;     mov gs, ax
;     mov ax, [esi + 4]       ; Load FS (offset 4)
;     mov fs, ax
;     mov ax, [esi + 8]       ; Load ES (offset 8)
;     mov es, ax
;     mov ax, [esi + 12]      ; Load DS (offset 12)
;     mov ds, ax

;     ; Load the interrupt number and error code into the correct registers
;     mov eax, [esi + 40]     ; Load int_no (offset 40)
;     push eax                ; Push the interrupt number onto the stack

;     mov eax, [esi + 44]     ; Load err_code (offset 44)
;     push eax                ; Push the error code onto the stack

; ; The rest of the logic for handling the interrupt follows...
;     ; Ensure the rest of the handler is set up to process the pushed values

;     ; Call the C handler (for demonstration purposes)
;     push dword [esi + 48]   ; Push eip (offset 48)
;     push dword [esi + 52]   ; Push cs (offset 52)
;     push dword [esi + 56]   ; Push eflags (offset 56)
;     push dword [esi + 60]   ; Push useresp (offset 60)
;     push dword [esi + 64]   ; Push ss (offset 64)

;     ; Return to caller
;     ret


    ; Jump table for IRQs
    cmp al, 0x20           ; IRQ0 - System Timer
    je irq0
    cmp al, 0x21           ; IRQ1 - Keyboard
    je irq1
    cmp al, 0x22           ; IRQ2 - Cascade (used for chained PICs)
    je irq2
    cmp al, 0x23           ; IRQ3 - COM2 (or COM4)
    je irq3
    cmp al, 0x24           ; IRQ4 - COM1 (or COM3)
    je irq4
    cmp al, 0x25           ; IRQ5 - LPT2 or sound card (varies by configuration)
    je irq5
    cmp al, 0x26           ; IRQ6 - Floppy Disk Controller
    je irq6
    cmp al, 0x27           ; IRQ7 - LPT1 or spurious interrupt
    je irq7
    cmp al, 0x28           ; IRQ8 - Real Time Clock
    je irq8
    cmp al, 0x29           ; IRQ9 - Redirected IRQ2 (used for ACPI and other devices)
    je irq9
    cmp al, 0x2A           ; IRQ10 - Reserved for peripherals (e.g., network card)
    je irq10
    cmp al, 0x2B           ; IRQ11 - Reserved for peripherals (e.g., SCSI or USB)
    je irq11
    cmp al, 0x2C           ; IRQ12 - PS/2 Mouse
    je irq12
    cmp al, 0x2D           ; IRQ13 - FPU (Floating Point Unit) / Coprocessor
    je irq13
    cmp al, 0x2E           ; IRQ14 - Primary ATA Hard Disk
    je irq14
    cmp al, 0x2F           ; IRQ15 - Secondary ATA Hard Disk
    je irq15

;     ; If the interrupt number is not recognized, return to the caller
     ret

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
    pusha                   ; Save all general-purpose registers
    push ds
    push es
    push fs
    push gs

    ; Set up the data segment registers
    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Load the current stack pointer into EAX to pass as an argument
    mov eax, esp
    push eax                ; Push pointer to the registers struct
    mov eax, irq_handler    ; Address of the C handler function
    call eax                ; Call the C handler
    add esp, 4              ; Clean up the argument from the stack

    ; Restore segment registers and general-purpose registers
    pop gs
    pop fs
    pop es
    pop ds
    popa                    ; Restore all general-purpose registers

    add esp, 8              ; Clean up stack for int_no and err_code
    iret                    ; Return from the interrupt
