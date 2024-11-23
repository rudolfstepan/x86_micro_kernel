section .text
global context_switch

; void context_switch(uint32_t** old_sp, uint32_t* new_sp)
; Arguments:
;   old_sp: Pointer to save the current stack pointer
;   new_sp: Pointer to load the new stack pointer

context_switch:
    pusha                           ; Save all general-purpose registers
    mov eax, [esp + 4]              ; Get the address of old_sp (first argument)
    mov [eax], esp                  ; Save the current ESP to *old_sp

    mov eax, [esp + 8]              ; Get the address of new_sp (second argument)
    test eax, eax                   ; Check if new_sp is NULL
    jz .restore_done                ; If NULL, skip restoring stack

    mov esp, [eax]                  ; Load the new ESP from *new_sp
    popa                            ; Restore all general-purpose registers
.restore_done:
    ret                             ; Return to the new context
