; Bounded Multiboot-v1 physical-memory foundation for the isolated x86_64
; bootstrap. The 32-bit half captures and validates the handoff before paging;
; the 64-bit half provides a fixed single-CPU frame allocator and self-test.

MULTIBOOT_BOOT_MAGIC equ 0x2BADB002
MULTIBOOT_MMAP_FLAG equ (1 << 6)
MULTIBOOT_MODS_FLAG equ (1 << 3)
MB_INFO_CAPTURE_SIZE equ 116
MB_INFO_FLAGS        equ 0
MB_INFO_MODS_COUNT   equ 20
MB_INFO_MODS_ADDR    equ 24
MB_INFO_MMAP_LENGTH  equ 44
MB_INFO_MMAP_ADDR    equ 48
MMAP_ENTRY_MIN_SIZE equ 20
MMAP_ENTRY_TYPE     equ 20
MAX_MMAP_BYTES      equ 4096
MAX_MMAP_ENTRIES    equ 128
MAX_MODULES         equ 32
MODULE_ENTRY_SIZE   equ 16

FRAME_SIZE          equ 4096
MANAGED_LIMIT       equ 0x04000000
FRAME_COUNT         equ 16384
FRAME_BITMAP_BYTES  equ 2048
DIRECT_MAP_BASE     equ 0xFFFF800000000000
DIRECT_PT_COUNT     equ 32
DIRECT_TABLE_COUNT  equ (2 + DIRECT_PT_COUNT)
PAGE_PRESENT_WRITE equ 0x003
PAGE_NX_HIGH        equ 0x80000000

BITS 32

section .text
global x86_64_physical_memory_init32
global x86_64_physical_memory_selftest64
extern pml4_table
extern _x86_64_bootstrap_end
extern serial_write64

x86_64_physical_memory_init32:
    cld
    cmp eax, MULTIBOOT_BOOT_MAGIC
    jne physical_memory_fail32
    test ebx, ebx
    jz physical_memory_fail32
    mov edx, ebx
    add edx, MB_INFO_CAPTURE_SIZE
    jc physical_memory_fail32
    mov dword [multiboot_info_address], ebx

    mov ecx, dword [ebx + MB_INFO_FLAGS]
    mov dword [multiboot_flags], ecx
    test ecx, MULTIBOOT_MMAP_FLAG
    jz physical_memory_fail32

    mov eax, dword [ebx + MB_INFO_MMAP_LENGTH]
    test eax, eax
    jz physical_memory_fail32
    cmp eax, MAX_MMAP_BYTES
    ja physical_memory_fail32
    mov esi, dword [ebx + MB_INFO_MMAP_ADDR]
    test esi, esi
    jz physical_memory_fail32
    mov edi, esi
    add edi, eax
    jc physical_memory_fail32
    mov dword [multiboot_mmap_address], esi
    mov dword [multiboot_mmap_length], eax
    mov dword [multiboot_mmap_end], edi

    xor eax, eax
    mov edi, usable_bitmap
    mov ecx, (2 * FRAME_BITMAP_BYTES) / 4
    rep stosd
    mov dword [managed_frame_count], 0
    mov dword [free_frame_count], 0

    call parse_usable_pass32
    jc physical_memory_fail32
    call parse_reserved_pass32
    jc physical_memory_fail32

    ; The entire bootstrap image, including its fixed paging and bitmap state,
    ; must never receive a writable direct-map alias.
    xor eax, eax
    mov edx, _x86_64_bootstrap_end
    call reserve_range32
    jc physical_memory_fail32

    mov eax, dword [multiboot_info_address]
    mov edx, MB_INFO_CAPTURE_SIZE
    call reserve_range32
    jc physical_memory_fail32
    mov eax, dword [multiboot_mmap_address]
    mov edx, dword [multiboot_mmap_length]
    call reserve_range32
    jc physical_memory_fail32

    mov ebx, dword [multiboot_info_address]
    mov eax, dword [multiboot_flags]
    test eax, MULTIBOOT_MODS_FLAG
    jz .modules_done
    mov ecx, dword [ebx + MB_INFO_MODS_COUNT]
    cmp ecx, MAX_MODULES
    ja physical_memory_fail32
    test ecx, ecx
    jz .modules_done
    mov esi, dword [ebx + MB_INFO_MODS_ADDR]
    test esi, esi
    jz physical_memory_fail32
    mov ebp, ecx
    mov eax, ecx
    shl eax, 4
    jc physical_memory_fail32
    mov edx, eax
    mov eax, esi
    call reserve_range32
    jc physical_memory_fail32

.module_loop:
    mov eax, dword [esi]
    mov edx, dword [esi + 4]
    cmp edx, eax
    jb physical_memory_fail32
    sub edx, eax
    call reserve_range32
    jc physical_memory_fail32
    add esi, MODULE_ENTRY_SIZE
    dec ebp
    jnz .module_loop
.modules_done:

    xor ecx, ecx
    xor edx, edx
.count_loop:
    bt dword [usable_bitmap], ecx
    adc edx, 0
    inc ecx
    cmp ecx, FRAME_COUNT
    jb .count_loop
    test edx, edx
    jz physical_memory_fail32
    mov dword [managed_frame_count], edx
    mov dword [free_frame_count], edx

    call build_direct_map32
    mov eax, 1
    ret

physical_memory_fail32:
    xor eax, eax
    mov edi, usable_bitmap
    mov ecx, (2 * FRAME_BITMAP_BYTES) / 4
    rep stosd
    mov dword [managed_frame_count], 0
    mov dword [free_frame_count], 0
    xor eax, eax
    ret

parse_usable_pass32:
    mov byte [parse_reserved_mode], 0
    jmp parse_memory_map32

parse_reserved_pass32:
    mov byte [parse_reserved_mode], 1

parse_memory_map32:
    mov esi, dword [multiboot_mmap_address]
    mov edi, dword [multiboot_mmap_end]
    xor ebp, ebp
.entry_loop:
    cmp esi, edi
    je .done
    ja .fail
    mov eax, edi
    sub eax, esi
    cmp eax, 4
    jb .fail
    mov edx, dword [esi]
    cmp edx, MMAP_ENTRY_MIN_SIZE
    jb .fail
    add edx, 4
    jc .fail
    cmp edx, eax
    ja .fail
    inc ebp
    cmp ebp, MAX_MMAP_ENTRIES
    ja .fail
    push edx

    mov eax, dword [esi + 4]
    mov edx, dword [esi + 8]
    mov ebx, dword [esi + 12]
    mov ecx, dword [esi + 16]
    add ebx, eax
    adc ecx, edx
    jc .entry_fail

    cmp byte [parse_reserved_mode], 0
    jne .reserved_pass
    cmp dword [esi + MMAP_ENTRY_TYPE], 1
    jne .entry_done
    mov byte [range_set_operation], 1
    call apply_managed_range32
    jmp .entry_done
.reserved_pass:
    cmp dword [esi + MMAP_ENTRY_TYPE], 1
    je .entry_done
    mov byte [range_set_operation], 0
    call apply_managed_range32
.entry_done:
    pop edx
    add esi, edx
    jmp .entry_loop
.entry_fail:
    pop edx
.fail:
    stc
    ret
.done:
    test ebp, ebp
    jz .fail
    clc
    ret

; Input range is [EDX:EAX, ECX:EBX). Only complete frames below 64 MiB can
; affect the bitmap. This routine preserves the parser cursor and entry count.
apply_managed_range32:
    push esi
    push edi
    push ebp
    test edx, edx
    jnz .done
    cmp eax, MANAGED_LIMIT
    jae .done
    test ecx, ecx
    jnz .clamp_end
    cmp ebx, MANAGED_LIMIT
    jbe .end_ready
.clamp_end:
    mov ebx, MANAGED_LIMIT
.end_ready:
    cmp eax, ebx
    jae .done
    cmp byte [range_set_operation], 0
    je .reserved_alignment
    add eax, FRAME_SIZE - 1
    jc .done
    and eax, -FRAME_SIZE
    and ebx, -FRAME_SIZE
    jmp .aligned
.reserved_alignment:
    and eax, -FRAME_SIZE
    add ebx, FRAME_SIZE - 1
    jc .reserved_clamp
    and ebx, -FRAME_SIZE
    cmp ebx, MANAGED_LIMIT
    jbe .aligned
.reserved_clamp:
    mov ebx, MANAGED_LIMIT
.aligned:
    cmp eax, ebx
    jae .done
    shr eax, 12
    shr ebx, 12
.frame_loop:
    cmp byte [range_set_operation], 0
    je .clear_frame
    bts dword [usable_bitmap], eax
    jmp .next_frame
.clear_frame:
    btr dword [usable_bitmap], eax
.next_frame:
    inc eax
    cmp eax, ebx
    jb .frame_loop
.done:
    pop ebp
    pop edi
    pop esi
    ret

; Reserve one 32-bit physical byte range. CF reports wraparound.
reserve_range32:
    mov ebx, eax
    add ebx, edx
    jc .overflow
    xor edx, edx
    xor ecx, ecx
    mov byte [range_set_operation], 0
    call apply_managed_range32
    clc
    ret
.overflow:
    stc
    ret

build_direct_map32:
    xor eax, eax
    mov edi, direct_pdpt
    mov ecx, (DIRECT_TABLE_COUNT * 4096) / 4
    rep stosd

    mov eax, direct_pdpt
    or eax, PAGE_PRESENT_WRITE
    mov dword [pml4_table + (256 * 8)], eax
    mov dword [pml4_table + (256 * 8) + 4], PAGE_NX_HIGH

    mov eax, direct_page_directory
    or eax, PAGE_PRESENT_WRITE
    mov dword [direct_pdpt], eax
    mov dword [direct_pdpt + 4], PAGE_NX_HIGH

    xor ecx, ecx
    mov esi, direct_page_tables
.directory_loop:
    mov eax, esi
    or eax, PAGE_PRESENT_WRITE
    mov dword [direct_page_directory + ecx * 8], eax
    mov dword [direct_page_directory + ecx * 8 + 4], PAGE_NX_HIGH
    add esi, 4096
    inc ecx
    cmp ecx, DIRECT_PT_COUNT
    jb .directory_loop

    xor eax, eax
.pte_loop:
    bt dword [usable_bitmap], eax
    jnc .next_pte
    mov edx, eax
    shl edx, 12
    or edx, PAGE_PRESENT_WRITE
    mov dword [direct_page_tables + eax * 8], edx
    mov dword [direct_page_tables + eax * 8 + 4], PAGE_NX_HIGH
.next_pte:
    inc eax
    cmp eax, FRAME_COUNT
    jb .pte_loop
    ret

BITS 64

x86_64_physical_memory_selftest64:
    mov eax, dword [rel free_frame_count]
    mov dword [rel selftest_initial_free], eax

    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, FRAME_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [rel selftest_frame0], rax

    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, FRAME_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [rel selftest_frame1], rax
    cmp rax, qword [rel selftest_frame0]
    je .fail

    call physical_frame_alloc64
    test rax, rax
    jz .fail
    test rax, FRAME_SIZE - 1
    jnz .fail
    cmp rax, MANAGED_LIMIT
    jae .fail
    mov qword [rel selftest_frame2], rax
    cmp rax, qword [rel selftest_frame0]
    je .fail
    cmp rax, qword [rel selftest_frame1]
    je .fail

    mov rdi, qword [rel selftest_frame0]
    mov rax, 0x0123456789ABCDEF
    call verify_direct_frame64
    test eax, eax
    jz .fail
    mov rdi, qword [rel selftest_frame1]
    mov rax, 0xF0E1D2C3B4A59687
    call verify_direct_frame64
    test eax, eax
    jz .fail
    mov rdi, qword [rel selftest_frame2]
    mov rax, 0x55AA55AA33CC33CC
    call verify_direct_frame64
    test eax, eax
    jz .fail

    mov rdi, qword [rel selftest_frame2]
    call physical_frame_free64
    test eax, eax
    jz .fail
    call physical_frame_alloc64
    cmp rax, qword [rel selftest_frame2]
    jne .fail
    mov qword [rel selftest_reused_frame], rax
    mov rdi, rax
    mov rax, 0xA5A5A5A55A5A5A5A
    call verify_direct_frame64
    test eax, eax
    jz .fail

    mov rdi, qword [rel selftest_reused_frame]
    call physical_frame_free64
    test eax, eax
    jz .fail
    mov rdi, qword [rel selftest_frame1]
    call physical_frame_free64
    test eax, eax
    jz .fail
    mov rdi, qword [rel selftest_frame0]
    call physical_frame_free64
    test eax, eax
    jz .fail

    mov rdi, 1
    call physical_frame_free64
    test eax, eax
    jnz .fail
    mov rdi, qword [rel selftest_frame0]
    call physical_frame_free64
    test eax, eax
    jnz .fail

    mov eax, dword [rel selftest_initial_free]
    cmp eax, dword [rel free_frame_count]
    jne .fail
    lea rsi, [rel physical_memory_ok_message]
    call serial_write64
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

physical_frame_alloc64:
    cld
    xor ecx, ecx
.scan:
    cmp ecx, FRAME_COUNT
    jae .none
    bt dword [rel usable_bitmap], ecx
    jnc .next
    bt dword [rel allocation_bitmap], ecx
    jc .next
    bts dword [rel allocation_bitmap], ecx
    dec dword [rel free_frame_count]
    mov eax, ecx
    shl rax, 12
    mov rdx, rax
    mov rdi, DIRECT_MAP_BASE
    add rdi, rax
    xor eax, eax
    mov ecx, FRAME_SIZE / 8
    rep stosq
    mov rax, rdx
    ret
.next:
    inc ecx
    jmp .scan
.none:
    xor eax, eax
    ret

physical_frame_free64:
    cld
    test rdi, FRAME_SIZE - 1
    jnz .invalid
    cmp rdi, MANAGED_LIMIT
    jae .invalid
    mov r8d, edi
    shr r8d, 12
    bt dword [rel usable_bitmap], r8d
    jnc .invalid
    bt dword [rel allocation_bitmap], r8d
    jnc .invalid
    mov rdx, rdi
    mov rdi, DIRECT_MAP_BASE
    add rdi, rdx
    xor eax, eax
    mov ecx, FRAME_SIZE / 8
    rep stosq
    btr dword [rel allocation_bitmap], r8d
    inc dword [rel free_frame_count]
    mov eax, 1
    ret
.invalid:
    xor eax, eax
    ret

verify_direct_frame64:
    mov rdx, DIRECT_MAP_BASE
    add rdx, rdi
    cmp qword [rdx], 0
    jne .bad
    mov qword [rdx], rax
    cmp qword [rdx], rax
    jne .bad
    mov eax, 1
    ret
.bad:
    xor eax, eax
    ret

section .rodata
physical_memory_ok_message db "REIST_X86_64_PHYSICAL_MEMORY_OK", 13, 10, 0

section .bss
alignb 16
multiboot_info_address:
    resd 1
multiboot_flags:
    resd 1
multiboot_mmap_address:
    resd 1
multiboot_mmap_length:
    resd 1
multiboot_mmap_end:
    resd 1
managed_frame_count:
    resd 1
free_frame_count:
    resd 1
selftest_initial_free:
    resd 1
parse_reserved_mode:
    resb 1
range_set_operation:
    resb 1

alignb 16
usable_bitmap:
    resb FRAME_BITMAP_BYTES
allocation_bitmap:
    resb FRAME_BITMAP_BYTES

alignb 4096
direct_pdpt:
    resb 4096
direct_page_directory:
    resb 4096
direct_page_tables:
    resb DIRECT_PT_COUNT * 4096

alignb 8
selftest_frame0:
    resq 1
selftest_frame1:
    resq 1
selftest_frame2:
    resq 1
selftest_reused_frame:
    resq 1
