[BITS 32]
global setjmp
global longjmp

section .text

; Save CPU context to TryContext
setjmp:
    mov eax, [esp + 4]       ; Load pointer to TryContext structure (argument)
    mov [eax], esp           ; Save ESP to ctx->esp
    mov [eax + 4], ebp       ; Save EBP to ctx->ebp
    mov edx, [esp]           ; Load return address (EIP)
    mov [eax + 8], edx       ; Save EIP to ctx->eip
    mov dword [eax + 12], 0  ; Initialize exception_code to 0
    xor eax, eax             ; Return 0 for normal flow
    ret

; Restore CPU context and jump to saved EIP
longjmp:
    mov eax, [esp + 4]       ; Load pointer to TryContext structure (argument)
    mov esp, [eax]           ; Restore ESP from ctx->esp
    mov ebp, [eax + 4]       ; Restore EBP from ctx->ebp
    mov edx, [eax + 8]       ; Load saved EIP into EDX
    mov ecx, [esp + 8]       ; Load exception code (second argument)
    mov [eax + 12], ecx      ; Store exception code in ctx->exception_code
    jmp edx                  ; Jump to the saved EIP