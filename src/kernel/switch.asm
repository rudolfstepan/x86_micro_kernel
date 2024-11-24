global swtch

section .text
swtch:
    mov eax, [esp + 4]    ; Load the first argument (current context pointer) into EAX
    mov edx, [esp + 8]    ; Load the second argument (next context pointer) into EDX

    ; Save old callee-saved registers
    push ebp              ; Save EBP
    push ebx              ; Save EBX
    push esi              ; Save ESI
    push edi              ; Save EDI

    ; Switch stacks
    mov [eax], esp        ; Save the old stack pointer in the current context
    mov esp, [edx]        ; Load the new stack pointer from the next context

    ; Load new callee-saved registers
    pop edi               ; Restore EDI
    pop esi               ; Restore ESI
    pop ebx               ; Restore EBX
    pop ebp               ; Restore EBP

    ret                   ; Return to the new context
