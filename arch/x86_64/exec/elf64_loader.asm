; Bounded ELF64 ET_EXEC validation and staging proof. The embedded payload is
; built independently by NASM and the ELF64 linker; it is never executed here.

%ifndef USER_PROBE_PATH
    %error "USER_PROBE_PATH must name the independently linked ELF64 probe"
%endif
%ifndef USER_SHELL_PATH
    %error "USER_SHELL_PATH must name the independently linked ELF64 shell"
%endif
%ifndef USER_CHILD_PATH
    %error "USER_CHILD_PATH must name the independently linked ELF64 child"
%endif

BITS 64

ELF_MAGIC           equ 0x464C457F
ELFCLASS64          equ 2
ELFDATA2LSB         equ 1
EV_CURRENT          equ 1
ELFOSABI_SYSV       equ 0
ET_EXEC             equ 2
EM_X86_64           equ 62
ELF64_EHDR_SIZE     equ 64
ELF64_PHDR_SIZE     equ 56
PT_NULL             equ 0
PT_LOAD             equ 1
PF_X                equ 1
PF_W                equ 2
PF_R                equ 4
MAX_EMBEDDED_BYTES  equ 65536
MAX_PROGRAM_HEADERS equ 4
MAX_LOAD_SEGMENTS   equ 2
USER_BASE           equ 0x00400000
USER_END            equ 0x00408000
USER_PAGE_COUNT     equ 8
PAGE_SIZE           equ 4096
DIRECT_MAP_BASE     equ 0xFFFF800000000000
ELF_IMAGE_PROBE     equ 0
ELF_IMAGE_SHELL     equ 1
ELF_IMAGE_CHILD     equ 2
ELF_CONTEXT_COUNT   equ 3
ELF_CONTEXT_SIZE    equ 88

EH_TYPE             equ 16
EH_MACHINE          equ 18
EH_VERSION          equ 20
EH_ENTRY            equ 24
EH_PHOFF            equ 32
EH_FLAGS            equ 48
EH_EHSIZE           equ 52
EH_PHENTSIZE        equ 54
EH_PHNUM            equ 56
PH_TYPE             equ 0
PH_FLAGS            equ 4
PH_OFFSET           equ 8
PH_VADDR            equ 16
PH_FILESZ           equ 32
PH_MEMSZ            equ 40
PH_ALIGN            equ 48

section .text
global x86_64_elf64_loader_selftest64
global x86_64_elf64_load64
global x86_64_elf64_release64
global x86_64_elf64_release_all64
global x86_64_elf64_entry64
global x86_64_elf64_page_frame64
global x86_64_elf64_page_flags64
global x86_64_elf64_address_flags64
global x86_64_elf64_select_image64
extern physical_frame_alloc64
extern physical_frame_free64
extern physical_free_frame_count64
extern serial_write64

x86_64_elf64_loader_selftest64:
    mov byte [rel elf_load_active], 0
    mov qword [rel elf_entry_address], 0
    xor edi, edi
    call x86_64_elf64_select_image64
    test eax, eax
    jz elf64_return_failure
    call x86_64_elf64_load64
    test eax, eax
    jz elf64_return_failure
    call x86_64_elf64_release64
    test eax, eax
    jz elf64_return_failure
    lea rsi, [rel elf64_load_ok_message]
    call serial_write64
    mov eax, 1
    ret

x86_64_elf64_load64:
    cmp byte [rel elf_load_active], 0
    jne elf64_return_failure
    cld
    call physical_free_frame_count64
    mov dword [rel elf_initial_free_count], eax
    mov byte [rel elf_load_active], 1

    xor eax, eax
    lea rdi, [rel elf_page_frames]
    mov ecx, USER_PAGE_COUNT
    rep stosq
    lea rdi, [rel elf_page_flags]
    mov ecx, USER_PAGE_COUNT
    rep stosb
    mov byte [rel elf_load_segment_count], 0
    mov byte [rel elf_entry_is_executable], 0
    mov qword [rel elf_entry_address], 0

    cmp byte [rel elf_image_selector], ELF_IMAGE_PROBE
    je .select_probe
    cmp byte [rel elf_image_selector], ELF_IMAGE_SHELL
    je .select_shell
    cmp byte [rel elf_image_selector], ELF_IMAGE_CHILD
    jne elf64_load_fail
    lea r12, [rel user_child_elf_start]
    mov r13, user_child_elf_end - user_child_elf_start
    jmp .image_selected
.select_shell:
    lea r12, [rel user_shell_elf_start]
    mov r13, user_shell_elf_end - user_shell_elf_start
    jmp .image_selected
.select_probe:
    lea r12, [rel user_probe_elf_start]
    mov r13, user_probe_elf_end - user_probe_elf_start
.image_selected:
    cmp r13, ELF64_EHDR_SIZE
    jb elf64_load_fail
    cmp r13, MAX_EMBEDDED_BYTES
    ja elf64_load_fail

    cmp dword [r12], ELF_MAGIC
    jne elf64_load_fail
    cmp byte [r12 + 4], ELFCLASS64
    jne elf64_load_fail
    cmp byte [r12 + 5], ELFDATA2LSB
    jne elf64_load_fail
    cmp byte [r12 + 6], EV_CURRENT
    jne elf64_load_fail
    cmp byte [r12 + 7], ELFOSABI_SYSV
    jne elf64_load_fail
    cmp byte [r12 + 8], 0
    jne elf64_load_fail
    cmp word [r12 + EH_TYPE], ET_EXEC
    jne elf64_load_fail
    cmp word [r12 + EH_MACHINE], EM_X86_64
    jne elf64_load_fail
    cmp dword [r12 + EH_VERSION], EV_CURRENT
    jne elf64_load_fail
    cmp dword [r12 + EH_FLAGS], 0
    jne elf64_load_fail
    cmp word [r12 + EH_EHSIZE], ELF64_EHDR_SIZE
    jne elf64_load_fail
    cmp word [r12 + EH_PHENTSIZE], ELF64_PHDR_SIZE
    jne elf64_load_fail

    movzx r14d, word [r12 + EH_PHNUM]
    test r14d, r14d
    jz elf64_load_fail
    cmp r14d, MAX_PROGRAM_HEADERS
    ja elf64_load_fail
    mov r15, qword [r12 + EH_PHOFF]
    cmp r15, ELF64_EHDR_SIZE
    jb elf64_load_fail
    mov rax, r14
    imul rax, ELF64_PHDR_SIZE
    jo elf64_load_fail
    add rax, r15
    jc elf64_load_fail
    mov rdx, r13
    cmp rax, rdx
    ja elf64_load_fail

    mov rbp, qword [r12 + EH_ENTRY]
    cmp rbp, USER_BASE
    jb elf64_load_fail
    cmp rbp, USER_END
    jae elf64_load_fail

    xor ebx, ebx
.validate_header_loop:
    cmp ebx, r14d
    jae .validation_done
    mov eax, ebx
    imul rax, ELF64_PHDR_SIZE
    lea rdi, [r12 + r15]
    add rdi, rax
    cmp dword [rdi + PH_TYPE], PT_NULL
    je .next_validation_header
    cmp dword [rdi + PH_TYPE], PT_LOAD
    jne elf64_load_fail

    inc byte [rel elf_load_segment_count]
    cmp byte [rel elf_load_segment_count], MAX_LOAD_SEGMENTS
    ja elf64_load_fail
    mov eax, dword [rdi + PH_FLAGS]
    test eax, ~7
    jnz elf64_load_fail
    test eax, PF_R
    jz elf64_load_fail
    mov edx, eax
    and edx, PF_W | PF_X
    cmp edx, PF_W | PF_X
    je elf64_load_fail
    cmp qword [rdi + PH_ALIGN], PAGE_SIZE
    jne elf64_load_fail
    mov r8, qword [rdi + PH_FILESZ]
    mov r9, qword [rdi + PH_MEMSZ]
    cmp r8, r9
    ja elf64_load_fail
    test r9, r9
    jz elf64_load_fail
    mov r10, qword [rdi + PH_OFFSET]
    mov r11, qword [rdi + PH_VADDR]
    mov rax, r10
    add rax, r8
    jc elf64_load_fail
    cmp rax, r13
    ja elf64_load_fail
    mov rdx, r11
    add rdx, r9
    jc elf64_load_fail
    cmp r11, USER_BASE
    jb elf64_load_fail
    cmp rdx, USER_END
    ja elf64_load_fail
    mov rax, r10
    xor rax, r11
    test rax, PAGE_SIZE - 1
    jnz elf64_load_fail

    mov rsi, r11
    sub rsi, USER_BASE
    shr rsi, 12
    mov rcx, rdx
    dec rcx
    sub rcx, USER_BASE
    shr rcx, 12
    cmp rcx, USER_PAGE_COUNT
    jae elf64_load_fail
    lea rdx, [rel elf_page_flags]
.mark_page_loop:
    cmp byte [rdx + rsi], 0
    jne elf64_load_fail
    mov al, byte [rdi + PH_FLAGS]
    mov byte [rdx + rsi], al
    inc rsi
    cmp rsi, rcx
    jbe .mark_page_loop

    test dword [rdi + PH_FLAGS], PF_X
    jz .next_validation_header
    cmp rbp, r11
    jb .next_validation_header
    mov rax, r11
    add rax, r9
    cmp rbp, rax
    jae .next_validation_header
    mov byte [rel elf_entry_is_executable], 1

.next_validation_header:
    inc ebx
    jmp .validate_header_loop

.validation_done:
    cmp byte [rel elf_load_segment_count], 0
    je elf64_load_fail
    cmp byte [rel elf_entry_is_executable], 1
    jne elf64_load_fail
    mov qword [rel elf_entry_address], rbp

    xor ecx, ecx
.allocation_loop:
    cmp ecx, USER_PAGE_COUNT
    jae .stage_segments
    lea rdx, [rel elf_page_flags]
    cmp byte [rdx + rcx], 0
    je .next_allocation
    push rcx
    call physical_frame_alloc64
    pop rcx
    test rax, rax
    jz elf64_load_fail
    lea rdx, [rel elf_page_frames]
    mov qword [rdx + rcx * 8], rax
.next_allocation:
    inc ecx
    jmp .allocation_loop

.stage_segments:
    xor ebx, ebx
.stage_header_loop:
    cmp ebx, r14d
    jae .verify_segments
    mov eax, ebx
    imul rax, ELF64_PHDR_SIZE
    lea rdi, [r12 + r15]
    add rdi, rax
    cmp dword [rdi + PH_TYPE], PT_LOAD
    jne .next_stage_header
    xor r8, r8
    mov r9, qword [rdi + PH_FILESZ]
    mov r10, qword [rdi + PH_VADDR]
    mov r11, qword [rdi + PH_OFFSET]
.copy_file_loop:
    cmp r8, r9
    jae .next_stage_header
    mov rax, r10
    add rax, r8
    call elf_user_direct_pointer64
    test rdx, rdx
    jz elf64_load_fail
    mov rax, r12
    add rax, r11
    mov cl, byte [rax + r8]
    mov byte [rdx], cl
    inc r8
    jmp .copy_file_loop
.next_stage_header:
    inc ebx
    jmp .stage_header_loop

.verify_segments:
    xor ebx, ebx
.verify_header_loop:
    cmp ebx, r14d
    jae .verified
    mov eax, ebx
    imul rax, ELF64_PHDR_SIZE
    lea rdi, [r12 + r15]
    add rdi, rax
    cmp dword [rdi + PH_TYPE], PT_LOAD
    jne .next_verify_header
    xor r8, r8
    mov r9, qword [rdi + PH_FILESZ]
    mov r10, qword [rdi + PH_VADDR]
    mov r11, qword [rdi + PH_OFFSET]
.verify_file_loop:
    cmp r8, r9
    jae .verify_zero_tail
    mov rax, r10
    add rax, r8
    call elf_user_direct_pointer64
    test rdx, rdx
    jz elf64_load_fail
    mov rax, r12
    add rax, r11
    mov cl, byte [rax + r8]
    cmp byte [rdx], cl
    jne elf64_load_fail
    inc r8
    jmp .verify_file_loop
.verify_zero_tail:
    mov r9, qword [rdi + PH_MEMSZ]
.zero_tail_loop:
    cmp r8, r9
    jae .next_verify_header
    mov rax, r10
    add rax, r8
    call elf_user_direct_pointer64
    test rdx, rdx
    jz elf64_load_fail
    cmp byte [rdx], 0
    jne elf64_load_fail
    inc r8
    jmp .zero_tail_loop
.next_verify_header:
    inc ebx
    jmp .verify_header_loop

.verified:
    mov eax, 1
    ret

elf64_load_fail:
    call elf64_cleanup64
elf64_return_failure:
    xor eax, eax
    ret

x86_64_elf64_release64:
    cmp byte [rel elf_load_active], 0
    je .already_released
    jmp elf64_cleanup64
.already_released:
    mov eax, 1
    ret

x86_64_elf64_release_all64:
    mov edi, ELF_IMAGE_CHILD
    call x86_64_elf64_select_image64
    test eax, eax
    jz .fail
    call x86_64_elf64_release64
    test eax, eax
    jz .fail
    mov edi, ELF_IMAGE_SHELL
    call x86_64_elf64_select_image64
    test eax, eax
    jz .fail
    call x86_64_elf64_release64
    test eax, eax
    jz .fail
    mov edi, ELF_IMAGE_PROBE
    call x86_64_elf64_select_image64
    test eax, eax
    jz .fail
    call x86_64_elf64_release64
    ret
.fail:
    xor eax, eax
    ret

x86_64_elf64_select_image64:
    cmp edi, ELF_IMAGE_CHILD
    ja .invalid
    cmp dil, byte [rel elf_image_selector]
    je .same
    push rdi
    call elf64_store_selected_context64
    pop rdi
    mov byte [rel elf_image_selector], dil
    call elf64_load_selected_context64
.same:
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

elf64_store_selected_context64:
    movzx eax, byte [rel elf_image_selector]
    imul eax, ELF_CONTEXT_SIZE
    lea rdi, [rel elf_context_store]
    add rdi, rax
    lea rsi, [rel elf_context_window]
    mov ecx, ELF_CONTEXT_SIZE / 8
    rep movsq
    ret

elf64_load_selected_context64:
    movzx eax, byte [rel elf_image_selector]
    imul eax, ELF_CONTEXT_SIZE
    lea rsi, [rel elf_context_store]
    add rsi, rax
    lea rdi, [rel elf_context_window]
    mov ecx, ELF_CONTEXT_SIZE / 8
    rep movsq
    ret

x86_64_elf64_entry64:
    cmp byte [rel elf_load_active], 1
    jne .invalid
    mov rax, qword [rel elf_entry_address]
    ret
.invalid:
    xor eax, eax
    ret

; ECX is a page index in the fixed eight-page ELF window.
x86_64_elf64_page_frame64:
    cmp byte [rel elf_load_active], 1
    jne .invalid
    cmp ecx, USER_PAGE_COUNT
    jae .invalid
    lea rax, [rel elf_page_frames]
    mov rax, qword [rax + rcx * 8]
    ret
.invalid:
    xor eax, eax
    ret

x86_64_elf64_page_flags64:
    cmp byte [rel elf_load_active], 1
    jne .invalid
    cmp ecx, USER_PAGE_COUNT
    jae .invalid
    lea rax, [rel elf_page_flags]
    movzx eax, byte [rax + rcx]
    ret
.invalid:
    xor eax, eax
    ret

; RAX is a user virtual address; EAX returns its validated ELF PF_* metadata.
x86_64_elf64_address_flags64:
    cmp byte [rel elf_load_active], 1
    jne .invalid
    cmp rax, USER_BASE
    jb .invalid
    cmp rax, USER_END
    jae .invalid
    sub rax, USER_BASE
    shr rax, 12
    lea rdx, [rel elf_page_flags]
    movzx eax, byte [rdx + rax]
    ret
.invalid:
    xor eax, eax
    ret

; RAX is a validated user virtual address; RDX returns its direct-map pointer.
elf_user_direct_pointer64:
    cmp rax, USER_BASE
    jb .invalid
    cmp rax, USER_END
    jae .invalid
    sub rax, USER_BASE
    mov rcx, rax
    shr rcx, 12
    lea rdx, [rel elf_page_frames]
    mov rdx, qword [rdx + rcx * 8]
    test rdx, rdx
    jz .invalid
    mov rcx, rax
    and ecx, PAGE_SIZE - 1
    mov rax, DIRECT_MAP_BASE
    add rdx, rax
    add rdx, rcx
    ret
.invalid:
    xor edx, edx
    ret

elf64_cleanup64:
    mov byte [rel elf_cleanup_error], 0
    xor ecx, ecx
.cleanup_loop:
    cmp ecx, USER_PAGE_COUNT
    jae .cleanup_count
    lea rdx, [rel elf_page_frames]
    mov rdi, qword [rdx + rcx * 8]
    test rdi, rdi
    jz .clear_metadata
    push rcx
    call physical_frame_free64
    pop rcx
    test eax, eax
    jnz .clear_frame
    mov byte [rel elf_cleanup_error], 1
    jmp .next_cleanup
.clear_frame:
    lea rdx, [rel elf_page_frames]
    mov qword [rdx + rcx * 8], 0
.clear_metadata:
    lea rdx, [rel elf_page_flags]
    mov byte [rdx + rcx], 0
.next_cleanup:
    inc ecx
    jmp .cleanup_loop
.cleanup_count:
    call physical_free_frame_count64
    cmp eax, dword [rel elf_initial_free_count]
    jne .cleanup_failed
    cmp byte [rel elf_cleanup_error], 0
    jne .cleanup_failed
    mov byte [rel elf_load_active], 0
    mov byte [rel elf_load_segment_count], 0
    mov byte [rel elf_entry_is_executable], 0
    mov qword [rel elf_entry_address], 0
    mov eax, 1
    ret
.cleanup_failed:
    xor eax, eax
    ret

section .rodata
align 16
user_probe_elf_start:
    incbin USER_PROBE_PATH
user_probe_elf_end:

align 16
user_shell_elf_start:
    incbin USER_SHELL_PATH
user_shell_elf_end:

align 16
user_child_elf_start:
    incbin USER_CHILD_PATH
user_child_elf_end:

elf64_load_ok_message db "REIST_X86_64_ELF64_LOAD_OK", 13, 10, 0

section .bss
alignb 16
elf_context_window:
elf_page_frames:
    resq USER_PAGE_COUNT
elf_page_flags:
    resb USER_PAGE_COUNT
elf_initial_free_count:
    resd 1
elf_load_segment_count:
    resb 1
elf_entry_is_executable:
    resb 1
elf_cleanup_error:
    resb 1
elf_load_active:
    resb 1
alignb 8
elf_entry_address:
    resq 1
elf_context_window_end:
elf_image_selector:
    resb 1
alignb 16
elf_context_store:
    resb ELF_CONTEXT_COUNT * ELF_CONTEXT_SIZE
