; @file kernel/sched/switch.asm
; @brief Sichert und wechselt den privilegierten Scheduler-Kontext.
;
; Layer: Ring-0 x86 context switch.
; Contract: swtch erhält Zeiger auf alten und neuen context_t; Callee-saved
;           Register und Stackpointer folgen exakt dem C-Strukturlayout.
; Safety: Aufruf nur bei schedulerseitig kontrolliertem IRQ- und Lockzustand.

global swtch

section .text
swtch:
    mov eax, [esp + 4]        ; Zeiger auf alten Kontext (context_t *old)
    mov edx, [esp + 8]        ; Zeiger auf neuen Kontext (context_t *new)

    ; Kernel-owned aligned contexts only. Corrupt internal ownership is fatal.
    test edx, edx
    jz .invalid_context
    test edx, 15
    jnz .invalid_context

    ; Alten Kontext sichern
    test eax, eax             ; Prüfe, ob old NULL ist
    jz .load_new_context      ; Wenn ja, überspringe das Speichern
    test eax, 15
    jnz .invalid_context
    ; Non-waiting: a pending unmasked user exception must not fault in Ring 0.
    fxsave [eax + 32]
    mov [eax + 0], esp        ; Speichere ESP
    mov [eax + 4], ebp        ; Speichere EBP
    mov [eax + 8], ebx        ; Speichere EBX
    mov [eax + 12], esi       ; Speichere ESI
    mov [eax + 16], edi       ; Speichere EDI

.load_new_context:
    ; Eager restore also on task exit (old == NULL), before any destination
    ; code or handoff can release/reuse the outgoing generation on another CPU.
    fxrstor [edx + 32]
    mov esp, [edx + 0]        ; Lade ESP
    mov ebp, [edx + 4]        ; Lade EBP
    mov ebx, [edx + 8]        ; Lade EBX
    mov esi, [edx + 12]       ; Lade ESI
    mov edi, [edx + 16]       ; Lade EDI

    ret                       ; Springe zu EIP im neuen Kontext

.invalid_context:
    ud2
