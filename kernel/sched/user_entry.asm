[BITS 32]
global enter_user_mode

section .text
enter_user_mode:
    cli
    mov eax, [esp + 4]
    mov edx, [esp + 8]

    mov cx, 0x23
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    push dword 0x23
    push edx
    pushfd
    or dword [esp], 0x200
    push dword 0x1B
    push eax
    iretd
