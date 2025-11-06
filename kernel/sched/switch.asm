global swtch

section .text
swtch:
    mov eax, [esp + 4]        ; Zeiger auf alten Kontext (context_t *old)
    mov edx, [esp + 8]        ; Zeiger auf neuen Kontext (context_t *new)

    ; Alten Kontext sichern
    test eax, eax             ; Prüfe, ob old NULL ist
    jz .load_new_context      ; Wenn ja, überspringe das Speichern
    mov [eax + 0], esp        ; Speichere ESP
    mov [eax + 4], ebp        ; Speichere EBP
    mov [eax + 8], ebx        ; Speichere EBX
    mov [eax + 12], esi       ; Speichere ESI
    mov [eax + 16], edi       ; Speichere EDI

.load_new_context:
    mov esp, [edx + 0]        ; Lade ESP
    mov ebp, [edx + 4]        ; Lade EBP
    mov ebx, [edx + 8]        ; Lade EBX
    mov esi, [edx + 12]       ; Lade ESI
    mov edi, [edx + 16]       ; Lade EDI

    ret                       ; Springe zu EIP im neuen Kontext
