; @file arch/x86/boot/bios/stage2_bios.asm
; @brief Nativer BIOS-Stage-2-Loader.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Liest Kernelmetadaten begrenzt, prüft CRC und lädt nur validierte ELF-Segmente.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
; Native BIOS stage 2 loader for the REIST OS.
;
; Entry contract from stage 1:
;   CS:IP = 1000:0000
;   DL    = BIOS boot drive
;   EAX   = absolute LBA of the active raw boot partition
;
; Stage 2 validates the image manifest, gathers an E820 memory map, loads all
; ELF32 PT_LOAD segments, zeros their BSS tails, and enters the existing
; Multiboot-1 kernel trampoline in 32-bit protected mode.

BITS 16
ORG 0

%macro BSWAP_EAX 0
    xchg al, ah
    rol eax, 16
    xchg al, ah
%endmacro

STAGE2_SEGMENT       equ 0x1000
STAGE2_PHYSICAL      equ 0x00010000
STACK_TOP            equ 0x0000F000
MB_INFO_ADDRESS      equ 0x00005000
MMAP_ADDRESS         equ 0x00006000
E820_TEMP_ADDRESS    equ 0x00007000
VBE_CTRL_INFO_ADDRESS equ 0x00008000
VBE_MODE_INFO_ADDRESS equ 0x00008200
VBE_RUNTIME_INFO_ADDRESS equ 0x00008400
BOUNCE_SEGMENT       equ 0x7000
BOUNCE_PHYSICAL      equ 0x00070000
BOUNCE_SIZE          equ 32768
KERNEL_CACHE_ADDRESS equ 0x00400000
KERNEL_CACHE_SIZE    equ 0x00100000       ; maximum boot ELF cached from floppy

MANIFEST_MAGIC_0     equ 0x42363858       ; "X86B"
MANIFEST_MAGIC_1     equ 0x32544F4F       ; "OOT2"
MANIFEST_VERSION     equ 2
MANIFEST_HEADER_SIZE equ 80
MANIFEST_STAGE2_LBA  equ 16
MANIFEST_STAGE2_CNT  equ 20
MANIFEST_KERNEL_LBA  equ 24
MANIFEST_KERNEL_SIZE equ 28
MANIFEST_PART_SIZE   equ 32
MANIFEST_KERNEL_CRC  equ 36
MANIFEST_FLAGS       equ 40
MANIFEST_KERNEL_SHA  equ 48

MULTIBOOT_MAGIC      equ 0x2BADB002
MULTIBOOT_FLAG_MEM   equ 0x001
MULTIBOOT_FLAG_BOOT  equ 0x002
MULTIBOOT_FLAG_MMAP  equ 0x040
MULTIBOOT_FLAG_NAME  equ 0x200
MULTIBOOT_FLAG_FRAMEBUFFER equ 0x1000

VBE_MODE_SUPPORTED   equ 0x0001
VBE_MODE_GRAPHICS    equ 0x0010
VBE_MODE_LFB         equ 0x0080
VBE_MEMORY_DIRECT    equ 0x06
VBE_LFB_REQUEST      equ 0x4000
VBE_RUNTIME_MAGIC    equ 0x52454256       ; "VBER"
VBE_RUNTIME_VERSION  equ 1
VBE_RUNTIME_SIZE     equ 40

ELF_MAGIC            equ 0x464C457F
ELF_PT_LOAD          equ 1
ELF_PF_EXECUTE       equ 1
MAX_PROGRAM_HEADERS  equ 16
MAX_LOAD_ADDRESS     equ 0x04000000       ; 64 MiB, includes user image area
COM1_BASE            equ 0x03F8

CODE_SELECTOR        equ 0x08
DATA_SELECTOR        equ 0x10
CODE16_SELECTOR      equ 0x18

start:
    cli
    mov ebp, eax                    ; preserve partition LBA across setup
    mov ax, cs
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti
    cld

    mov [boot_drive], dl
    mov [partition_lba], ebp
    call serial_init
    mov ax, 0x0003
    int 0x10
    mov si, msg_loader
    call print_string

    call enable_a20
    call build_multiboot_info

    ; Re-read and validate the complete manifest sector.
    mov eax, [partition_lba]
    mov cx, 1
    call read_bounce
    jc disk_error
    mov ax, BOUNCE_SEGMENT
    mov es, ax
    cmp dword [es:0], MANIFEST_MAGIC_0
    jne manifest_error
    cmp dword [es:4], MANIFEST_MAGIC_1
    jne manifest_error
    cmp dword [es:8], MANIFEST_VERSION
    jne manifest_error
    cmp dword [es:12], MANIFEST_HEADER_SIZE
    jne manifest_error
    cmp dword [es:MANIFEST_FLAGS], 0
    jne manifest_error

    xor eax, eax
    xor si, si
    mov cx, 128
.manifest_checksum:
    add eax, [es:si]
    add si, 4
    loop .manifest_checksum
    test eax, eax
    jnz manifest_error

    ; Require and preserve the digest before the bounce buffer is reused.
    xor eax, eax
    mov si, MANIFEST_KERNEL_SHA
    mov cx, 8
.manifest_sha:
    or eax, [es:si]
    add si, 4
    loop .manifest_sha
    test eax, eax
    jz manifest_error
    mov si, MANIFEST_KERNEL_SHA
    xor di, di
    mov cx, 8
.copy_manifest_sha:
    mov eax, [es:si]
    mov [cs:kernel_sha256 + di], eax
    add si, 4
    add di, 4
    loop .copy_manifest_sha

    mov eax, [es:MANIFEST_KERNEL_LBA]
    mov [kernel_relative_lba], eax
    mov eax, [es:MANIFEST_KERNEL_SIZE]
    mov [kernel_size], eax
    cmp eax, 4096
    jb manifest_error
    mov eax, [es:MANIFEST_PART_SIZE]
    mov [partition_size], eax
    mov eax, [es:MANIFEST_KERNEL_CRC]
    mov [kernel_crc], eax

    ; Kernel extent must fit completely inside the active boot partition.
    mov eax, [kernel_size]
    add eax, 511
    jc manifest_error
    shr eax, 9
    add eax, [kernel_relative_lba]
    jc manifest_error
    cmp eax, [partition_size]
    ja manifest_error
    mov eax, [partition_lba]
    add eax, [kernel_relative_lba]
    jc manifest_error
    mov [kernel_lba], eax

    mov si, msg_verify
    call print_string
    call verify_kernel_integrity
    jnc .integrity_valid
    cmp byte [integrity_error], 2
    je sha_error
    jmp crc_error
.integrity_valid:

    mov si, msg_kernel
    call print_string

    ; ELF header and program-header table are required to fit in 4 KiB.
    cmp byte [kernel_cached], 1
    je .header_from_cache
    mov eax, [kernel_lba]
    mov cx, 8
    call read_bounce
    jc disk_error
    jmp .header_ready
.header_from_cache:
    mov dword [pm_source], KERNEL_CACHE_ADDRESS
    mov dword [pm_destination], BOUNCE_PHYSICAL
    mov dword [pm_length], 4096
    mov byte [pm_operation], 0
    call protected_memory_operation
.header_ready:
    call parse_elf_header
    jc elf_error
    call load_elf_segments
    jc load_error
    cmp byte [entry_is_executable], 1
    jne elf_error

    ; Probe and validate a fixed runtime VBE mode while BIOS services are
    ; still directly available.  This does not change the VGA text mode.
    call prepare_vbe_runtime
    mov si, msg_start
    call print_string
%ifdef USE_FRAMEBUFFER
    ; Keep BIOS text diagnostics available until the kernel is fully loaded.
    ; The VBE switch is the final real-mode operation before entering it.
    call setup_vbe_framebuffer
%endif
    jmp enter_kernel

disk_error:
    mov si, msg_disk_error
    jmp fatal
manifest_error:
    mov si, msg_manifest_error
    jmp fatal
elf_error:
    mov si, msg_elf_error
    jmp fatal
load_error:
    mov si, msg_load_error
    jmp fatal
crc_error:
    mov si, msg_crc_error
    jmp fatal
sha_error:
    mov si, msg_sha_error

fatal:
    call print_string
    cli
.halt:
    hlt
    jmp .halt

enable_a20:
    ; Try the BIOS service first, then unconditionally enable the fast A20
    ; gate. Both QEMU and VMware implement at least the latter.
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    ret

build_multiboot_info:
    xor ax, ax
    mov es, ax
    mov di, MB_INFO_ADDRESS
    mov cx, 116 / 2
    xor ax, ax
    rep stosw

    mov dword [es:MB_INFO_ADDRESS], MULTIBOOT_FLAG_MEM | MULTIBOOT_FLAG_BOOT | MULTIBOOT_FLAG_NAME
    movzx eax, byte [boot_drive]
    shl eax, 24
    or eax, 0x00FFFFFF
    mov [es:MB_INFO_ADDRESS + 12], eax
    int 0x12
    movzx eax, ax
    mov [es:MB_INFO_ADDRESS + 4], eax

    ; Conservative fallback if E820 is unavailable.
    mov dword [es:MB_INFO_ADDRESS + 8], 63 * 1024
    mov ah, 0x88
    int 0x15
    jc .basic_done
    movzx eax, ax
    test eax, eax
    jz .basic_done
    mov [es:MB_INFO_ADDRESS + 8], eax
.basic_done:
    mov dword [es:MB_INFO_ADDRESS + 64], STAGE2_PHYSICAL + bootloader_name
    call collect_e820
    ret

collect_e820:
    xor ebx, ebx
    mov word [mmap_entries], 0
    mov word [mmap_cursor], MMAP_ADDRESS
    mov dword [mmap_length], 0
.next:
    cmp word [mmap_entries], 32
    jae .done
    xor ax, ax
    mov es, ax
    mov di, E820_TEMP_ADDRESS
    mov dword [es:di + 20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150             ; "SMAP"
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .skip
    mov eax, [es:E820_TEMP_ADDRESS + 8]
    or eax, [es:E820_TEMP_ADDRESS + 12]
    jz .skip
    cmp ecx, 20
    jbe .accept
    test byte [es:E820_TEMP_ADDRESS + 20], 1
    jz .skip
.accept:
    ; Use the contiguous usable region beginning at 1 MiB for mem_upper.
    cmp dword [es:E820_TEMP_ADDRESS + 16], 1
    jne .copy
    cmp dword [es:E820_TEMP_ADDRESS], 0x00100000
    jne .copy
    cmp dword [es:E820_TEMP_ADDRESS + 4], 0
    jne .copy
    cmp dword [es:E820_TEMP_ADDRESS + 12], 0
    jne .copy
    mov eax, [es:E820_TEMP_ADDRESS + 8]
    shr eax, 10
    mov [es:MB_INFO_ADDRESS + 8], eax
.copy:
    mov di, [mmap_cursor]
    mov dword [es:di], 20
    add di, 4
    push ds
    xor ax, ax
    mov ds, ax
    mov si, E820_TEMP_ADDRESS
    mov cx, 10
    rep movsw
    pop ds
    add word [mmap_cursor], 24
    add dword [mmap_length], 24
    inc word [mmap_entries]
.skip:
    test ebx, ebx
    jnz .next
.done:
    cmp word [mmap_entries], 0
    je .return
    xor ax, ax
    mov es, ax
    or dword [es:MB_INFO_ADDRESS], MULTIBOOT_FLAG_MMAP
    mov eax, [mmap_length]
    mov [es:MB_INFO_ADDRESS + 44], eax
    mov dword [es:MB_INFO_ADDRESS + 48], MMAP_ADDRESS
.return:
    ret

; Select a linear, direct-colour VBE mode and publish the Multiboot-1
; framebuffer extension.  1024x768x32 is preferred; 800x600x32 is the
; compatibility fallback.  Any malformed/unsupported response returns to
; VGA mode 03 and deliberately leaves the framebuffer flag clear.
setup_vbe_framebuffer:
    call probe_vbe_mode
    jc .failed

.mode_found:
    mov bx, [vbe_selected_mode]
    or bx, VBE_LFB_REQUEST
    mov ax, 0x4F02
    int 0x10
    push cs
    pop ds
    cmp ax, 0x004F
    jne .failed

    ; Publish offsets 88..115 only after the requested LFB mode is active.
    ; The VBE physical base is 32-bit, so the upper half of framebuffer_addr
    ; is explicitly zeroed for Multiboot's 64-bit field.
    xor ax, ax
    mov es, ax
    mov eax, [vbe_selected_address]
    mov [es:MB_INFO_ADDRESS + 88], eax
    mov dword [es:MB_INFO_ADDRESS + 92], 0
    movzx eax, word [vbe_selected_pitch]
    mov [es:MB_INFO_ADDRESS + 96], eax
    movzx eax, word [vbe_target_width]
    mov [es:MB_INFO_ADDRESS + 100], eax
    movzx eax, word [vbe_target_height]
    mov [es:MB_INFO_ADDRESS + 104], eax
    mov byte [es:MB_INFO_ADDRESS + 108], 32
    mov byte [es:MB_INFO_ADDRESS + 109], 1 ; direct RGB colour
    mov al, [vbe_red_position]
    mov [es:MB_INFO_ADDRESS + 110], al
    mov al, [vbe_red_size]
    mov [es:MB_INFO_ADDRESS + 111], al
    mov al, [vbe_green_position]
    mov [es:MB_INFO_ADDRESS + 112], al
    mov al, [vbe_green_size]
    mov [es:MB_INFO_ADDRESS + 113], al
    mov al, [vbe_blue_position]
    mov [es:MB_INFO_ADDRESS + 114], al
    mov al, [vbe_blue_size]
    mov [es:MB_INFO_ADDRESS + 115], al
    or dword [es:MB_INFO_ADDRESS], MULTIBOOT_FLAG_FRAMEBUFFER
    ret

.failed:
    ; A failed set-mode call can leave firmware in an indeterminate display
    ; state.  Restore the universally available VGA text mode and invalidate
    ; every framebuffer byte before continuing with the kernel fallback.
    mov ax, 0x0003
    int 0x10
    push cs
    pop ds
    xor ax, ax
    mov es, ax
    and dword [es:MB_INFO_ADDRESS], 0xFFFFEFFF
    mov di, MB_INFO_ADDRESS + 88
    mov cx, 28 / 2
    xor ax, ax
    cld
    rep stosw
    ret

; Publish only metadata selected by the same bounded validator used by the
; framebuffer boot.  The runtime kernel thunk accepts no caller-selected mode.
prepare_vbe_runtime:
    push cs
    pop ds
    xor ax, ax
    mov es, ax
    mov di, VBE_RUNTIME_INFO_ADDRESS
    mov cx, VBE_RUNTIME_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    call probe_vbe_mode
    jc .return
    xor ax, ax
    mov es, ax
    mov dword [es:VBE_RUNTIME_INFO_ADDRESS + 0], VBE_RUNTIME_MAGIC
    mov dword [es:VBE_RUNTIME_INFO_ADDRESS + 4], VBE_RUNTIME_VERSION
    mov dword [es:VBE_RUNTIME_INFO_ADDRESS + 8], VBE_RUNTIME_SIZE
    mov ax, [vbe_selected_mode]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 12], ax
    mov eax, [vbe_selected_address]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 16], eax
    movzx eax, word [vbe_selected_pitch]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 20], eax
    movzx eax, word [vbe_target_width]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 24], eax
    movzx eax, word [vbe_target_height]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 28], eax
    mov byte [es:VBE_RUNTIME_INFO_ADDRESS + 32], 32
    mov byte [es:VBE_RUNTIME_INFO_ADDRESS + 33], 1
    mov al, [vbe_red_position]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 34], al
    mov al, [vbe_red_size]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 35], al
    mov al, [vbe_green_position]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 36], al
    mov al, [vbe_green_size]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 37], al
    mov al, [vbe_blue_position]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 38], al
    mov al, [vbe_blue_size]
    mov [es:VBE_RUNTIME_INFO_ADDRESS + 39], al
.return:
    ret

probe_vbe_mode:
    push cs
    pop ds
    cld
    xor ax, ax
    mov es, ax
    mov di, VBE_CTRL_INFO_ADDRESS
    mov cx, 512 / 2
    xor ax, ax
    rep stosw
    mov dword [es:VBE_CTRL_INFO_ADDRESS], 0x32454256 ; "VBE2"
    mov di, VBE_CTRL_INFO_ADDRESS
    mov ax, 0x4F00
    int 0x10
    push cs
    pop ds
    cmp ax, 0x004F
    jne .not_found
    xor ax, ax
    mov es, ax
    cmp dword [es:VBE_CTRL_INFO_ADDRESS], 0x41534556 ; "VESA"
    jne .not_found

    mov word [vbe_target_width], 1024
    mov word [vbe_target_height], 768
    call find_vbe_mode
    jnc .found
    mov word [vbe_target_width], 800
    mov word [vbe_target_height], 600
    call find_vbe_mode
    jc .not_found
.found:
    clc
    ret
.not_found:
    stc
    ret

; Find a matching 32-bit direct-colour mode in the controller's mode list.
; Input is vbe_target_width/height; carry is clear only for a valid LFB mode.
find_vbe_mode:
    xor ax, ax
    mov es, ax
    mov ax, [es:VBE_CTRL_INFO_ADDRESS + 14]
    mov [vbe_mode_list_offset], ax
    mov ax, [es:VBE_CTRL_INFO_ADDRESS + 16]
    mov [vbe_mode_list_segment], ax
    mov word [vbe_modes_examined], 0

.next_mode:
    cmp word [vbe_modes_examined], 512
    jae .not_found
    mov ax, [vbe_mode_list_segment]
    mov fs, ax
    mov si, [vbe_mode_list_offset]
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .not_found
    mov [vbe_candidate_mode], cx
    add word [vbe_mode_list_offset], 2
    jnc .pointer_advanced
    add word [vbe_mode_list_segment], 0x1000
.pointer_advanced:
    inc word [vbe_modes_examined]

    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_INFO_ADDRESS
    push cx
    mov cx, 256 / 2
    xor ax, ax
    cld
    rep stosw
    pop cx
    mov di, VBE_MODE_INFO_ADDRESS
    mov ax, 0x4F01
    int 0x10
    push cs
    pop ds
    cmp ax, 0x004F
    jne .next_mode
    xor ax, ax
    mov es, ax

    mov ax, [es:VBE_MODE_INFO_ADDRESS]
    and ax, VBE_MODE_SUPPORTED | VBE_MODE_GRAPHICS | VBE_MODE_LFB
    cmp ax, VBE_MODE_SUPPORTED | VBE_MODE_GRAPHICS | VBE_MODE_LFB
    jne .next_mode
    mov ax, [es:VBE_MODE_INFO_ADDRESS + 18]
    cmp ax, [vbe_target_width]
    jne .next_mode
    mov ax, [es:VBE_MODE_INFO_ADDRESS + 20]
    cmp ax, [vbe_target_height]
    jne .next_mode
    cmp byte [es:VBE_MODE_INFO_ADDRESS + 24], 1
    jne .next_mode
    cmp byte [es:VBE_MODE_INFO_ADDRESS + 25], 32
    jne .next_mode
    cmp byte [es:VBE_MODE_INFO_ADDRESS + 27], VBE_MEMORY_DIRECT
    jne .next_mode
    cmp dword [es:VBE_MODE_INFO_ADDRESS + 40], 0
    je .next_mode

    ; VBE 3 supplies LFB-specific pitch/masks.  VBE 2 leaves those bytes zero,
    ; in which case the original direct-colour fields are authoritative.
    mov ax, [es:VBE_MODE_INFO_ADDRESS + 50]
    test ax, ax
    jnz .pitch_ready
    mov ax, [es:VBE_MODE_INFO_ADDRESS + 16]
.pitch_ready:
    mov dx, [vbe_target_width]
    shl dx, 2
    cmp ax, dx
    jb .next_mode
    mov [vbe_selected_pitch], ax

    cmp byte [es:VBE_MODE_INFO_ADDRESS + 54], 0
    je .standard_masks
    cmp byte [es:VBE_MODE_INFO_ADDRESS + 56], 0
    je .standard_masks
    cmp byte [es:VBE_MODE_INFO_ADDRESS + 58], 0
    je .standard_masks
    mov al, [es:VBE_MODE_INFO_ADDRESS + 54]
    mov [vbe_red_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 55]
    mov [vbe_red_position], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 56]
    mov [vbe_green_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 57]
    mov [vbe_green_position], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 58]
    mov [vbe_blue_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 59]
    mov [vbe_blue_position], al
    jmp .masks_ready

.standard_masks:
    mov al, [es:VBE_MODE_INFO_ADDRESS + 31]
    mov [vbe_red_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 32]
    mov [vbe_red_position], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 33]
    mov [vbe_green_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 34]
    mov [vbe_green_position], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 35]
    mov [vbe_blue_size], al
    mov al, [es:VBE_MODE_INFO_ADDRESS + 36]
    mov [vbe_blue_position], al

.masks_ready:
    ; Match framebuffer_init's channel contract before changing hardware
    ; modes.  Use 16-bit arithmetic so hostile VBE bytes cannot wrap an
    ; 8-bit position + size check.
    movzx ax, byte [vbe_red_size]
    test ax, ax
    jz .next_mode
    cmp ax, 32
    ja .next_mode
    movzx dx, byte [vbe_red_position]
    cmp dx, 32
    jae .next_mode
    add ax, dx
    cmp ax, 32
    ja .next_mode
    mov [vbe_red_end], ax

    movzx ax, byte [vbe_green_size]
    test ax, ax
    jz .next_mode
    cmp ax, 32
    ja .next_mode
    movzx dx, byte [vbe_green_position]
    cmp dx, 32
    jae .next_mode
    add ax, dx
    cmp ax, 32
    ja .next_mode
    mov [vbe_green_end], ax

    movzx ax, byte [vbe_blue_size]
    test ax, ax
    jz .next_mode
    cmp ax, 32
    ja .next_mode
    movzx dx, byte [vbe_blue_position]
    cmp dx, 32
    jae .next_mode
    add ax, dx
    cmp ax, 32
    ja .next_mode
    mov [vbe_blue_end], ax

    ; Direct-colour channels are contiguous bit ranges and must be disjoint.
    movzx ax, byte [vbe_red_position]
    movzx bx, byte [vbe_green_position]
    cmp word [vbe_red_end], bx
    jbe .red_green_separate
    cmp word [vbe_green_end], ax
    ja .next_mode
.red_green_separate:
    movzx bx, byte [vbe_blue_position]
    cmp word [vbe_red_end], bx
    jbe .red_blue_separate
    cmp word [vbe_blue_end], ax
    ja .next_mode
.red_blue_separate:
    movzx ax, byte [vbe_green_position]
    cmp word [vbe_green_end], bx
    jbe .channels_separate
    cmp word [vbe_blue_end], ax
    ja .next_mode
.channels_separate:

    ; map_kernel_mmio uses an identity mapping and rejects overflow or any
    ; physical range intersecting the process-private 1..3-GiB window.
    movzx eax, word [vbe_selected_pitch]
    movzx ecx, word [vbe_target_height]
    mul ecx
    test edx, edx
    jnz .next_mode
    test eax, eax
    jz .next_mode
    mov ebx, [es:VBE_MODE_INFO_ADDRESS + 40]
    add eax, ebx
    jc .next_mode
    cmp eax, 0x40000000
    jbe .lfb_range_valid
    cmp ebx, 0xC0000000
    jb .next_mode
.lfb_range_valid:

    mov ax, [vbe_candidate_mode]
    mov [vbe_selected_mode], ax
    mov eax, [es:VBE_MODE_INFO_ADDRESS + 40]
    mov [vbe_selected_address], eax
    clc
    ret

.not_found:
    stc
    ret

parse_elf_header:
    mov ax, BOUNCE_SEGMENT
    mov es, ax
    cmp dword [es:0], ELF_MAGIC
    jne .bad
    cmp byte [es:4], 1              ; ELFCLASS32
    jne .bad
    cmp byte [es:5], 1              ; little endian
    jne .bad
    cmp byte [es:6], 1
    jne .bad
    cmp word [es:16], 2             ; ET_EXEC
    jne .bad
    cmp word [es:18], 3             ; EM_386
    jne .bad
    cmp dword [es:20], 1
    jne .bad
    cmp word [es:42], 32
    jne .bad
    mov ax, [es:44]
    test ax, ax
    jz .bad
    cmp ax, MAX_PROGRAM_HEADERS
    ja .bad
    mov [program_header_count], ax
    mov eax, [es:28]
    cmp eax, 52
    jb .bad
    mov [program_header_offset], eax
    movzx edx, word [program_header_count]
    shl edx, 5
    add edx, eax
    jc .bad
    cmp edx, 4096
    ja .bad
    cmp edx, [kernel_size]
    ja .bad
    mov eax, [es:24]
    mov [kernel_entry], eax

    ; Preserve the PH table before the shared bounce buffer is reused.
    mov ax, ds
    mov es, ax
    push ds
    mov ax, BOUNCE_SEGMENT
    mov ds, ax
    mov si, [cs:program_header_offset]
    mov di, program_headers
    mov cx, [cs:program_header_count]
    shl cx, 4                       ; 32 bytes / 2
    rep movsw
    pop ds
    clc
    ret
.bad:
    stc
    ret

load_elf_segments:
    mov byte [entry_is_executable], 0
    mov word [program_header_index], 0
.next:
    mov ax, [program_header_index]
    cmp ax, [program_header_count]
    jae .done
    shl ax, 5
    mov si, program_headers
    add si, ax
    inc word [program_header_index]
    cmp dword [si], ELF_PT_LOAD
    jne .next

    mov eax, [si + 16]              ; p_filesz
    cmp eax, [si + 20]              ; p_memsz
    ja .bad
    mov [segment_file_size], eax
    mov eax, [si + 20]
    test eax, eax
    jz .next
    mov [segment_memory_size], eax
    mov eax, [si + 8]               ; p_vaddr
    cmp eax, [si + 12]              ; p_paddr
    jne .bad
    cmp eax, 0x00100000
    jb .bad
    mov [segment_address], eax
    mov edx, eax
    add edx, [segment_memory_size]
    jc .bad
    cmp edx, MAX_LOAD_ADDRESS
    ja .bad
    mov [segment_end], edx

    ; A cached floppy image remains the source until a PT_LOAD destination
    ; overlaps it.  The ELF header and program headers have already been copied
    ; into stage-2 memory, so invalidate the cache and reload this and all later
    ; ranges through the bounce buffer before the destination overwrites it.
    cmp byte [kernel_cached], 1
    jne .cache_overlap_checked
    cmp edx, KERNEL_CACHE_ADDRESS
    jbe .cache_overlap_checked
    mov eax, KERNEL_CACHE_ADDRESS
    add eax, [kernel_size]
    cmp [segment_address], eax
    jae .cache_overlap_checked
    mov byte [kernel_cached], 0
.cache_overlap_checked:

    mov eax, [si + 4]               ; p_offset
    mov [range_file_offset], eax
    add eax, [segment_file_size]
    jc .bad
    cmp eax, [kernel_size]
    ja .bad

    test dword [si + 24], ELF_PF_EXECUTE
    jz .not_entry
    mov eax, [kernel_entry]
    cmp eax, [segment_address]
    jb .not_entry
    cmp eax, [segment_end]
    jae .not_entry
    mov byte [entry_is_executable], 1
.not_entry:
    mov eax, [segment_address]
    mov [range_destination], eax
    mov eax, [segment_file_size]
    mov [range_remaining], eax
    test eax, eax
    jz .zero_tail
    call load_file_range
    jc .bad

.zero_tail:
    mov eax, [segment_address]
    add eax, [segment_file_size]
    mov [pm_destination], eax
    mov eax, [segment_memory_size]
    sub eax, [segment_file_size]
    mov [pm_length], eax
    test eax, eax
    jz .next
    mov byte [pm_operation], 1
    call protected_memory_operation
    jmp .next
.done:
    clc
    ret
.bad:
    stc
    ret

load_file_range:
    cmp byte [kernel_cached], 1
    jne .next
    mov eax, KERNEL_CACHE_ADDRESS
    add eax, [range_file_offset]
    jc .bad
    mov [pm_source], eax
    mov eax, [range_destination]
    mov [pm_destination], eax
    mov eax, [range_remaining]
    mov [pm_length], eax
    mov byte [pm_operation], 0
    call protected_memory_operation
    mov eax, [range_remaining]
    add [range_file_offset], eax
    add [range_destination], eax
    mov dword [range_remaining], 0
    jmp .done
.next:
    mov eax, [range_remaining]
    test eax, eax
    jz .done
    mov ebx, [range_file_offset]
    and ebx, 511
    mov edx, BOUNCE_SIZE
    sub edx, ebx
    cmp eax, edx
    jbe .length_ready
    mov eax, edx
.length_ready:
    mov [pm_length], eax
    mov edx, BOUNCE_PHYSICAL
    add edx, ebx
    mov [pm_source], edx
    mov edx, [range_destination]
    mov [pm_destination], edx

    mov edx, eax
    add edx, ebx
    add edx, 511
    jc .bad
    shr edx, 9
    cmp edx, 64
    ja .bad
    mov cx, dx
    mov eax, [range_file_offset]
    shr eax, 9
    add eax, [kernel_lba]
    jc .bad
    call read_bounce
    jc .bad

    mov byte [pm_operation], 0
    call protected_memory_operation
    mov eax, [pm_length]
    add [range_file_offset], eax
    add [range_destination], eax
    sub [range_remaining], eax
    jmp .next
.done:
    clc
    ret
.bad:
    stc
    ret

; Read CX sectors at absolute LBA EAX into the 32-KiB bounce buffer.
read_bounce:
    cmp byte [boot_drive], 0x80
    jb read_bounce_chs
    mov [read_lba_value], eax
    mov [read_count], cx
    mov byte [read_retries], 3
.retry:
    mov ax, [read_count]
    mov [dap_count], ax
    mov word [dap_offset], 0
    mov word [dap_segment], BOUNCE_SEGMENT
    mov eax, [read_lba_value]
    mov [dap_lba], eax
    mov dword [dap_lba + 4], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    dec byte [read_retries]
    jnz .retry
    mov ax, ds
    mov es, ax
    stc
    ret
.ok:
    mov ax, ds
    mov es, ax
    clc
    ret

; Legacy floppy BIOSes commonly provide no INT 13h extensions. Read as many
; sectors as remain on the current track using standard 18-sector/2-head CHS
; geometry. Requests never cross a track because many real BIOSes and VMware's
; physical-floppy backend reject or serialize cross-track transfers.
read_bounce_chs:
    mov [read_lba_value], eax
    mov [read_count], cx
    mov ax, BOUNCE_SEGMENT
    mov es, ax
    xor bx, bx
.next_sector:
    cmp word [read_count], 0
    je .ok
    mov eax, [read_lba_value]
    xor edx, edx
    mov ecx, 36
    div ecx                         ; EAX=cylinder, EDX=track remainder
    cmp eax, 79
    ja .bad
    mov [chs_cylinder], al
    mov eax, edx
    xor edx, edx
    mov ecx, 18
    div ecx                         ; EAX=head, EDX=sector index
    mov [chs_head], al
    mov [chs_sector], dl

    mov eax, 18
    sub eax, edx                    ; sectors left on this track
    cmp ax, [read_count]
    jbe .count_ready
    mov ax, [read_count]
.count_ready:
    mov [chs_transfer_count], al

    mov dh, [chs_head]
    mov cl, [chs_sector]
    inc cl
    mov ch, [chs_cylinder]
    mov dl, [boot_drive]
    mov al, [chs_transfer_count]
    mov ah, 0x02
    int 0x13
    jc .retry_sector
    movzx ax, byte [chs_transfer_count]
    sub [read_count], ax
    movzx eax, ax
    add [read_lba_value], eax
    shl ax, 9
    add bx, ax
    mov byte [read_retries], 0
    jmp .next_sector
.retry_sector:
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    mov eax, [read_lba_value]
    ; Retry through the normal loop; a persistent error is bounded by the
    ; BIOS reset succeeding only transiently on real drives.
    inc byte [read_retries]
    cmp byte [read_retries], 3
    jbe .next_sector
.bad:
    mov ax, ds
    mov es, ax
    stc
    ret
.ok:
    mov ax, ds
    mov es, ax
    mov byte [read_retries], 0
    clc
    ret

; Verify SHA-256 and diagnostic CRC32 over the exact complete ELF file,
; including bytes which are not covered by PT_LOAD segments.  Both digests use
; the same bounded 32-KiB read/cache pass.
verify_kernel_integrity:
    mov byte [integrity_error], 0
    call sha256_init
    mov byte [kernel_cached], 0
    cmp byte [boot_drive], 0x80
    jae .cache_ready
    mov eax, [kernel_size]
    cmp eax, KERNEL_CACHE_SIZE
    ja .cache_ready
    xor ax, ax
    mov es, ax
    cmp dword [es:MB_INFO_ADDRESS + 8], 4096
    jb .cache_ready
    mov byte [kernel_cached], 1
    mov dword [cache_write_address], KERNEL_CACHE_ADDRESS
.cache_ready:
    mov eax, [kernel_lba]
    mov [crc_lba], eax
    mov eax, [kernel_size]
    mov [crc_remaining], eax
    mov dword [crc_value], 0xFFFFFFFF
.next_chunk:
    mov eax, [crc_remaining]
    test eax, eax
    jz .complete
    cmp eax, BOUNCE_SIZE
    jbe .size_ready
    mov eax, BOUNCE_SIZE
.size_ready:
    mov [crc_chunk_size], ax
    mov edx, eax
    add edx, 511
    shr edx, 9
    mov [crc_sector_count], dx
    mov cx, dx
    mov eax, [crc_lba]
    call read_bounce
    jc .bad

    cmp byte [kernel_cached], 1
    jne .checksum_chunk
    mov dword [pm_source], BOUNCE_PHYSICAL
    mov eax, [cache_write_address]
    mov [pm_destination], eax
    movzx eax, word [crc_chunk_size]
    mov [pm_length], eax
    mov byte [pm_operation], 0
    call protected_memory_operation
    mov eax, [pm_length]
    add [cache_write_address], eax

.checksum_chunk:
    call sha256_process_chunk
    mov ax, BOUNCE_SEGMENT
    mov es, ax
    xor di, di
    mov cx, [crc_chunk_size]
    mov eax, [crc_value]
.next_byte:
    movzx ebx, byte [es:di]
    inc di
    xor al, bl
    mov ebx, eax
    and ebx, 0x0F
    shr eax, 4
    xor eax, [cs:crc32_nibble_table + ebx * 4]
    mov ebx, eax
    and ebx, 0x0F
    shr eax, 4
    xor eax, [cs:crc32_nibble_table + ebx * 4]
    loop .next_byte
    mov [crc_value], eax

    movzx eax, word [crc_sector_count]
    add [crc_lba], eax
    movzx eax, word [crc_chunk_size]
    sub [crc_remaining], eax
    jmp .next_chunk
.complete:
    call sha256_finalize
    jc .sha_bad
    mov eax, [crc_value]
    not eax
    cmp eax, [kernel_crc]
    jne .crc_bad
    mov ax, ds
    mov es, ax
    clc
    ret
.sha_bad:
    mov byte [integrity_error], 2
    jmp .bad
.crc_bad:
    mov byte [integrity_error], 1
.bad:
    mov ax, ds
    mov es, ax
    stc
    ret

; Initialize the eight FIPS 180-4 SHA-256 chaining words.
sha256_init:
    mov dword [sha256_state + 0], 0x6A09E667
    mov dword [sha256_state + 4], 0xBB67AE85
    mov dword [sha256_state + 8], 0x3C6EF372
    mov dword [sha256_state + 12], 0xA54FF53A
    mov dword [sha256_state + 16], 0x510E527F
    mov dword [sha256_state + 20], 0x9B05688C
    mov dword [sha256_state + 24], 0x1F83D9AB
    mov dword [sha256_state + 28], 0x5BE0CD19
    mov word [sha256_tail_size], 0
    ret

; Consume the current bounce-buffer chunk. BOUNCE_SIZE is divisible by 64, so
; only the final kernel chunk can leave a tail.
sha256_process_chunk:
    push ax
    push bx
    push cx
    push di
    mov ax, BOUNCE_SEGMENT
    mov es, ax
    xor di, di
    mov cx, [crc_chunk_size]
.next_block:
    cmp cx, 64
    jb .copy_tail
    call sha256_transform
    add di, 64
    sub cx, 64
    jmp .next_block
.copy_tail:
    mov [sha256_tail_size], cx
    xor bx, bx
.tail_byte:
    test cx, cx
    jz .done
    mov al, [es:di]
    mov [cs:sha256_block + bx], al
    inc di
    inc bx
    dec cx
    jmp .tail_byte
.done:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; Append FIPS 180-4 padding and compare the resulting big-endian digest with
; the fixed manifest copy. The bit length is represented as a full 64-bit
; value even though the current manifest kernel_size field is 32 bit.
sha256_finalize:
    mov ax, ds
    mov es, ax
    mov bx, [sha256_tail_size]
    mov byte [sha256_block + bx], 0x80
    inc bx
    cmp bx, 56
    jbe .pad_length
.pad_first_block:
    cmp bx, 64
    jae .first_ready
    mov byte [sha256_block + bx], 0
    inc bx
    jmp .pad_first_block
.first_ready:
    mov di, sha256_block
    call sha256_transform
    xor bx, bx
.pad_length:
    cmp bx, 56
    jae .length_ready
    mov byte [sha256_block + bx], 0
    inc bx
    jmp .pad_length
.length_ready:
    mov eax, [kernel_size]
    mov edx, eax
    shl eax, 3
    shr edx, 29
    mov ecx, eax
    mov eax, edx
    BSWAP_EAX
    mov [sha256_block + 56], eax
    mov eax, ecx
    BSWAP_EAX
    mov [sha256_block + 60], eax
    mov di, sha256_block
    call sha256_transform

    xor ebx, ebx
.compare:
    mov eax, [sha256_state + ebx]
    BSWAP_EAX
    cmp eax, [kernel_sha256 + ebx]
    jne .bad
    add ebx, 4
    cmp ebx, 32
    jb .compare
    clc
    ret
.bad:
    stc
    ret

; Compress one 64-byte block at ES:DI. The 64-word schedule and all working
; state are fixed stage-2 storage; PUSHAD only uses the bounded boot stack.
sha256_transform:
    pushad
    xor ebx, ebx
.load_words:
    mov eax, [es:di + bx]
    BSWAP_EAX
    mov [cs:sha256_schedule + ebx], eax
    add ebx, 4
    cmp ebx, 64
    jb .load_words

.expand_words:
    mov edx, [cs:sha256_schedule + ebx - 60]
    mov eax, edx
    ror eax, 7
    mov ecx, edx
    ror ecx, 18
    xor eax, ecx
    shr edx, 3
    xor eax, edx
    mov esi, eax

    mov edx, [cs:sha256_schedule + ebx - 8]
    mov eax, edx
    ror eax, 17
    mov ecx, edx
    ror ecx, 19
    xor eax, ecx
    shr edx, 10
    xor eax, edx
    add eax, esi
    add eax, [cs:sha256_schedule + ebx - 64]
    add eax, [cs:sha256_schedule + ebx - 28]
    mov [cs:sha256_schedule + ebx], eax
    add ebx, 4
    cmp ebx, 256
    jb .expand_words

    mov eax, [cs:sha256_state + 0]
    mov [cs:sha256_a], eax
    mov eax, [cs:sha256_state + 4]
    mov [cs:sha256_b], eax
    mov eax, [cs:sha256_state + 8]
    mov [cs:sha256_c], eax
    mov eax, [cs:sha256_state + 12]
    mov [cs:sha256_d], eax
    mov eax, [cs:sha256_state + 16]
    mov [cs:sha256_e], eax
    mov eax, [cs:sha256_state + 20]
    mov [cs:sha256_f], eax
    mov eax, [cs:sha256_state + 24]
    mov [cs:sha256_g], eax
    mov eax, [cs:sha256_state + 28]
    mov [cs:sha256_h], eax

    xor ebx, ebx
.round:
    mov edx, [cs:sha256_e]
    mov eax, edx
    ror eax, 6
    mov ecx, edx
    ror ecx, 11
    xor eax, ecx
    mov ecx, edx
    ror ecx, 25
    xor eax, ecx
    add eax, [cs:sha256_h]

    mov edx, [cs:sha256_e]
    mov ecx, [cs:sha256_f]
    xor ecx, [cs:sha256_g]
    and edx, ecx
    xor edx, [cs:sha256_g]
    add eax, edx
    add eax, [cs:sha256_constants + ebx]
    add eax, [cs:sha256_schedule + ebx]
    mov esi, eax

    mov edx, [cs:sha256_a]
    mov eax, edx
    ror eax, 2
    mov ecx, edx
    ror ecx, 13
    xor eax, ecx
    mov ecx, edx
    ror ecx, 22
    xor eax, ecx

    mov edx, [cs:sha256_a]
    and edx, [cs:sha256_b]
    mov ecx, [cs:sha256_a]
    or ecx, [cs:sha256_b]
    and ecx, [cs:sha256_c]
    or edx, ecx
    add eax, edx

    mov edx, [cs:sha256_g]
    mov [cs:sha256_h], edx
    mov edx, [cs:sha256_f]
    mov [cs:sha256_g], edx
    mov edx, [cs:sha256_e]
    mov [cs:sha256_f], edx
    mov edx, [cs:sha256_d]
    add edx, esi
    mov [cs:sha256_e], edx
    mov edx, [cs:sha256_c]
    mov [cs:sha256_d], edx
    mov edx, [cs:sha256_b]
    mov [cs:sha256_c], edx
    mov edx, [cs:sha256_a]
    mov [cs:sha256_b], edx
    add eax, esi
    mov [cs:sha256_a], eax

    add ebx, 4
    cmp ebx, 256
    jb .round

    mov eax, [cs:sha256_a]
    add [cs:sha256_state + 0], eax
    mov eax, [cs:sha256_b]
    add [cs:sha256_state + 4], eax
    mov eax, [cs:sha256_c]
    add [cs:sha256_state + 8], eax
    mov eax, [cs:sha256_d]
    add [cs:sha256_state + 12], eax
    mov eax, [cs:sha256_e]
    add [cs:sha256_state + 16], eax
    mov eax, [cs:sha256_f]
    add [cs:sha256_state + 20], eax
    mov eax, [cs:sha256_g]
    add [cs:sha256_state + 24], eax
    mov eax, [cs:sha256_h]
    add [cs:sha256_state + 28], eax
    popad
    ret

; Perform either a flat 32-bit copy (operation 0) or zero-fill (operation 1),
; then return to real mode for the next BIOS disk call.
protected_memory_operation:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SELECTOR:(STAGE2_PHYSICAL + copy_protected)

BITS 32
copy_protected:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    cld
    mov edi, [STAGE2_PHYSICAL + pm_destination]
    mov ecx, [STAGE2_PHYSICAL + pm_length]
    cmp byte [STAGE2_PHYSICAL + pm_operation], 0
    jne .zero
    mov esi, [STAGE2_PHYSICAL + pm_source]
    rep movsb
    jmp .leave
.zero:
    xor eax, eax
    rep stosb
.leave:
    ; Switch to a D=0 protected-mode code descriptor before clearing PE.
    ; Jumping directly from a D=1 segment into real mode leaves some CPUs and
    ; emulators decoding the return path as 32-bit code.
    jmp dword CODE16_SELECTOR:(STAGE2_PHYSICAL + copy_leave16)

BITS 16
copy_leave16:
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp STAGE2_SEGMENT:copy_real

copy_real:
    mov ax, cs
    mov ds, ax
    mov es, ax
    ; The protected-mode copy helper disables interrupts while changing CR0.
    ; BIOS disk services used by the following chunk may need IRQ delivery
    ; (including VMware disk backends), so restore the real-mode IF contract.
    sti
    ret

enter_kernel:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SELECTOR:(STAGE2_PHYSICAL + kernel_protected)

BITS 32
kernel_protected:
    mov ax, DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000
    cld
    mov eax, MULTIBOOT_MAGIC
    mov ebx, MB_INFO_ADDRESS
    mov edx, [STAGE2_PHYSICAL + kernel_entry]
    jmp edx

BITS 16
serial_init:
    ; 115200 baud, 8 data bits, no parity, one stop bit.  The serial mirror is
    ; useful on headless VMware runs and remains harmless on machines without
    ; a legacy COM1 UART.
    push ax
    push dx
    mov dx, COM1_BASE + 1
    xor al, al
    out dx, al                       ; disable UART interrupts
    mov dx, COM1_BASE + 3
    mov al, 0x80
    out dx, al                       ; enable divisor latch
    mov dx, COM1_BASE
    mov al, 1
    out dx, al                       ; divisor 1 = 115200 baud
    mov dx, COM1_BASE + 1
    xor al, al
    out dx, al
    mov dx, COM1_BASE + 3
    mov al, 0x03
    out dx, al                       ; 8-N-1
    mov dx, COM1_BASE + 2
    mov al, 0xC7
    out dx, al                       ; enable and clear FIFOs
    mov dx, COM1_BASE + 4
    mov al, 0x0B
    out dx, al                       ; DTR, RTS, OUT2
    pop dx
    pop ax
    ret

; Write AL to COM1.  Bound the readiness wait so absent or unusual hardware
; cannot stop the boot process.
serial_write_char:
    push ax
    push cx
    push dx
    mov dx, COM1_BASE + 5
    mov cx, 0xFFFF
.wait:
    in al, dx
    test al, 0x20
    jnz .ready
    loop .wait
.ready:
    pop dx
    pop cx
    pop ax
    push dx
    mov dx, COM1_BASE
    out dx, al
    pop dx
    ret

print_string:
    lodsb
    test al, al
    jz .done
    out 0xE9, al                    ; QEMU/Bochs debug console, harmless elsewhere
    call serial_write_char           ; VMware/headless and physical COM1 trace
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF            ; flat 32-bit code
    dq 0x00CF92000000FFFF            ; flat 32-bit data
    dq 0x008F9A000000FFFF            ; flat 16-bit transition code
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt - 1
    dd STAGE2_PHYSICAL + (gdt - $$)

align 4
dap:
    db 0x10, 0
dap_count              dw 0
dap_offset             dw 0
dap_segment            dw 0
dap_lba                dq 0

boot_drive             db 0
read_retries           db 0
read_count             dw 0
mmap_entries           dw 0
mmap_cursor            dw 0
program_header_count   dw 0
program_header_index   dw 0
entry_is_executable    db 0
pm_operation           db 0
kernel_cached          db 0
integrity_error        db 0             ; 1=CRC32, 2=SHA-256
vbe_red_size           db 0
vbe_red_position       db 0
vbe_green_size         db 0
vbe_green_position     db 0
vbe_blue_size          db 0
vbe_blue_position      db 0

align 4
partition_lba          dd 0
partition_size         dd 0
kernel_relative_lba    dd 0
kernel_lba             dd 0
kernel_size            dd 0
kernel_entry           dd 0
program_header_offset  dd 0
mmap_length            dd 0
read_lba_value         dd 0
segment_file_size      dd 0
segment_memory_size    dd 0
segment_address        dd 0
segment_end            dd 0
range_file_offset      dd 0
range_destination      dd 0
range_remaining        dd 0
pm_source              dd 0
pm_destination         dd 0
pm_length              dd 0
crc_lba                dd 0
crc_remaining          dd 0
crc_value              dd 0
kernel_crc             dd 0
cache_write_address    dd 0
vbe_selected_address   dd 0
vbe_mode_list_offset   dw 0
vbe_mode_list_segment  dw 0
vbe_modes_examined     dw 0
vbe_candidate_mode     dw 0
vbe_selected_mode      dw 0
vbe_target_width       dw 0
vbe_target_height      dw 0
vbe_selected_pitch     dw 0
vbe_red_end            dw 0
vbe_green_end          dw 0
vbe_blue_end           dw 0
crc_chunk_size         dw 0
crc_sector_count       dw 0
sha256_tail_size       dw 0
chs_cylinder           db 0
chs_head               db 0
chs_sector             db 0
chs_transfer_count     db 0

align 4
kernel_sha256:
    times 8 dd 0
sha256_state:
    times 8 dd 0
sha256_a               dd 0
sha256_b               dd 0
sha256_c               dd 0
sha256_d               dd 0
sha256_e               dd 0
sha256_f               dd 0
sha256_g               dd 0
sha256_h               dd 0
sha256_block:
    times 64 db 0

program_headers:
    times MAX_PROGRAM_HEADERS * 32 db 0

bootloader_name    db "REIST OS BIOS Loader", 0
msg_loader         db "x86 native BIOS loader", 13, 10, 0
msg_verify         db "Verifying kernel SHA-256/CRC32...", 13, 10, 0
msg_kernel         db "Loading ELF32 kernel...", 13, 10, 0
msg_start          db "Starting kernel...", 13, 10, 0
msg_disk_error     db "Disk read failed", 13, 10, 0
msg_manifest_error db "Invalid boot manifest", 13, 10, 0
msg_elf_error      db "Invalid ELF32 kernel", 13, 10, 0
msg_load_error     db "Kernel load failed", 13, 10, 0
msg_crc_error      db "Kernel CRC32 verification failed", 13, 10, 0
msg_sha_error      db "Kernel SHA-256 verification failed", 13, 10, 0

align 4
crc32_nibble_table:
    dd 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC
    dd 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C
    dd 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C
    dd 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C

align 4
sha256_constants:
    dd 0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5
    dd 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5
    dd 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3
    dd 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174
    dd 0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC
    dd 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA
    dd 0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7
    dd 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967
    dd 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13
    dd 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85
    dd 0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3
    dd 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070
    dd 0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5
    dd 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3
    dd 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208
    dd 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2

align 4
sha256_schedule:
    times 64 dd 0
