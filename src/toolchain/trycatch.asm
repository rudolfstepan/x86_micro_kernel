[BITS 32]

; Export both functions so they can be linked and used from C code
global setjmp
global longjmp

section .text

; --------------------------------------
; setjmp
; --------------------------------------
; Saves the CPU context (ESP, EBP, EIP) into a TryContext structure
; Arguments:
;   [esp + 4]: Pointer to TryContext
; Returns:
;   0 on initial call, or a non-zero value from longjmp
setjmp:
    mov eax, [esp + 4]       ; Load the pointer to the TryContext structure
    mov [eax], esp           ; Save ESP to ctx->esp
    mov [eax + 4], ebp       ; Save EBP to ctx->ebp
    mov edx, [esp]           ; Load the return address (EIP) from [esp + 0]
    mov [eax + 8], edx       ; Save EIP to ctx->eip
    mov dword [eax + 12], 0  ; Initialize exception_code to 0
    xor eax, eax             ; Return 0 to indicate normal flow
    ret
; --------------------------------------
; longjmp
; --------------------------------------
; Restores the CPU context (ESP, EBP, EIP) from a TryContext structure
; Arguments:
;   [esp + 4]: Pointer to TryContext
;   [esp + 8]: Exception code
longjmp:
    mov eax, [esp + 4]       ; Load the pointer to the TryContext structure
    mov esp, [eax]           ; Restore ESP from ctx->esp
    mov ebp, [eax + 4]       ; Restore EBP from ctx->ebp
    mov edx, [eax + 8]       ; Load the saved return address (EIP) into edx
    mov ecx, [esp + 8]       ; Load the exception code (argument) into ecx
    mov [eax + 12], ecx      ; Store the exception code in ctx->exception_code
    jmp edx                  ; Jump to the saved return address
