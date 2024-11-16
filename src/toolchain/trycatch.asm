[BITS 32]

global setjmp
global longjmp

section .text

; setjmp function definition
setjmp:
    ; Your setjmp implementation here
    mov eax, [esp + 4]       ; Get the pointer to the TryContext structure (first argument)
    mov [eax], esp           ; Save ESP to ctx->esp
    mov [eax + 4], ebp       ; Save EBP to ctx->ebp
    mov edx, [esp]           ; Load the return address (EIP) from [esp + 0] into edx
    mov [eax + 8], edx       ; Save return address (EIP) to ctx->eip
    mov dword [eax + 12], 0  ; Set exception_code to 0
    xor eax, eax             ; Return 0 to indicate normal flow
    ret

; longjmp function definition
longjmp:
    mov eax, [esp + 4]       ; Load the pointer to the TryContext structure
    mov esp, [eax]           ; Restore ESP from ctx->esp
    mov ebp, [eax + 4]       ; Restore EBP from ctx->ebp
    mov edx, [eax + 8]       ; Load the saved return address (EIP) into edx
    mov ecx, [eax + 12]      ; Load the exception code into ecx
    jmp edx                  ; Jump to the saved return address
