; @file arch/x86/boot/bios/stage1_mbr.asm
; @brief MBR-Stage-1-Loader.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Lädt eine feste Stage-2-Sektormenge und bleibt innerhalb des BIOS-Bootbudgets.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
; Native BIOS stage 1 loader.
; The image builder patches the partition table below and places a manifest
; in the first sector of the active boot partition.

BITS 16
ORG 0x7C00

STAGE2_SEGMENT      equ 0x1000
MANIFEST_SEGMENT    equ 0x0800
MANIFEST_MAGIC_0    equ 0x42363858      ; "X86B"
MANIFEST_MAGIC_1    equ 0x32544F4F      ; "OOT2"
MANIFEST_VERSION    equ 3
MANIFEST_HEADER_SIZE equ 336
MAX_STAGE2_SECTORS  equ 64

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    ; Extended INT 13h reads are required by stage 1 and are implemented by
    ; QEMU, VMware, and modern legacy/CSM BIOSes.
    mov bx, 0x55AA
    mov ah, 0x41
    int 0x13
    jc disk_extensions_missing
    cmp bx, 0xAA55
    jne disk_extensions_missing
    test cx, 1
    jz disk_extensions_missing

    ; Locate the active partition instead of assuming a fixed disk offset.
    mov si, partition_table
    mov cx, 4
.find_active:
    cmp byte [si], 0x80
    je .active_found
    add si, 16
    loop .find_active
    mov si, msg_partition
    jmp fatal

.active_found:
    mov eax, [si + 8]
    test eax, eax
    jz invalid_manifest
    mov [partition_lba], eax

    ; Read the partition manifest to 0000:8000.
    mov cx, 1
    mov bx, 0
    mov dx, MANIFEST_SEGMENT
    call read_lba
    jc disk_read_failed

    mov ax, MANIFEST_SEGMENT
    mov es, ax
    cmp dword [es:0], MANIFEST_MAGIC_0
    jne invalid_manifest
    cmp dword [es:4], MANIFEST_MAGIC_1
    jne invalid_manifest
    cmp dword [es:8], MANIFEST_VERSION
    jne invalid_manifest
    cmp dword [es:12], MANIFEST_HEADER_SIZE
    jne invalid_manifest

    mov ecx, [es:20]             ; stage2 sector count
    test ecx, ecx
    jz invalid_manifest
    cmp ecx, MAX_STAGE2_SECTORS
    ja invalid_manifest
    mov eax, [es:16]             ; stage2 LBA relative to partition
    add eax, [partition_lba]
    jc invalid_manifest

    ; Load stage 2 to physical 0001:0000 (1000:0000).
    mov bx, 0
    mov dx, STAGE2_SEGMENT
    call read_lba
    jc disk_read_failed

    mov dl, [boot_drive]
    mov eax, [partition_lba]
    jmp STAGE2_SEGMENT:0

disk_extensions_missing:
    mov si, msg_extensions
    jmp fatal

disk_read_failed:
    mov si, msg_read
    jmp fatal

invalid_manifest:
    mov si, msg_manifest

fatal:
    call print_string
    cli
.hang:
    hlt
    jmp .hang

; Read CX sectors at absolute LBA EAX to DX:BX. Carry is set after three
; unsuccessful attempts. The DAP is rebuilt because some BIOSes modify it.
read_lba:
    mov [read_lba_value], eax
    mov [read_count], cx
    mov [read_offset], bx
    mov [read_segment], dx
    mov byte [read_retries], 3
.retry:
    mov ax, [read_count]
    mov [dap_count], ax
    mov ax, [read_offset]
    mov [dap_offset], ax
    mov ax, [read_segment]
    mov [dap_segment], ax
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
    stc
    ret
.ok:
    clc
    ret

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string
.done:
    ret

boot_drive        db 0
read_retries      db 0
read_count        dw 0
read_offset       dw 0
read_segment      dw 0
partition_lba     dd 0
read_lba_value    dd 0

align 4
dap:
    db 0x10, 0
dap_count         dw 0
dap_offset        dw 0
dap_segment       dw 0
dap_lba           dq 0

msg_extensions db "No BIOS EDD", 13, 10, 0
msg_partition  db "No active boot partition", 13, 10, 0
msg_read       db "Boot disk read failed", 13, 10, 0
msg_manifest   db "Invalid boot manifest", 13, 10, 0

; A real MBR reserves bytes 446..509 for four partition entries. The image
; builder fills the first entry and leaves the remaining entries empty.
times 446 - ($ - $$) db 0
partition_table:
times 64 db 0
dw 0xAA55
