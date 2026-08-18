; @file arch/x86/boot/bootloader.asm
; @brief Multiboot-Kernelstart.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Initialisiert Minimalzustand und übergibt kontrolliert an den C-Kernel.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
[BITS 32]
global start
global interrupt

extern _stack_end
extern _stack_guard_start
extern kernel_main

section .text
start:
    cli
    cld
    ; Use the stack reserved by the kernel linker script.
    mov esp, _stack_end
    and esp, 0xfffffff0
    sub esp, 8                 ; Keep the i386 ABI's 16-byte call alignment
    ; Initialize the lower guard page before paging makes it non-present.
    ; Preserve the
    ; Multiboot handoff values in EAX/EBX on the freshly established stack.
    push eax
    push ebx
    mov edi, _stack_guard_start
    mov eax, 0x4B535447        ; "KSTG"
    mov ecx, 1024
    rep stosd
    pop ebx
    pop eax
    ; Debug markers used by the native BIOS smoke tests. E9 is consumed by
    ; QEMU/Bochs; COM1 is captured by the generated headless VMware machine.
    ; Preserve the Multiboot handoff registers below.
    push eax
    push ecx
    push edx
    mov dx, 0x00E9
    mov al, 'K'
    out dx, al
    mov al, 10
    out dx, al
    mov al, 'K'
    call serial_write_byte
    mov al, 10
    call serial_write_byte
    pop edx
    pop ecx
    pop eax
    ; Call the kernel main function
    push ebx
    push eax
    call kernel_main
    ; Halt the system if kernel_main returns
    cli
.halt:
    hlt
    jmp .halt

; Stage 2 has already configured COM1. Bound the poll so this marker can never
; stall a physical boot when no conventional UART is present.
serial_write_byte:
    mov ah, al
    mov dx, 0x03FD
    mov ecx, 0x0000FFFF
.wait:
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
.ready:
    mov al, ah
    mov dx, 0x03F8
    out dx, al
    ret
