; @file arch/x86/boot/bios/stage1_floppy.asm
; @brief Disketten-Stage-1-Loader.
;
; Layer: x86 boot and privilege-transition assembly.
; Contract: Konvertiert feste CHS-Geometrie und lädt Stage 2 mit begrenzten BIOS-Retries.
; Safety: Stacklayout, Sektorzahl und Poll-/Retryarbeit bleiben explizit begrenzt.
;
; 1.44-MB BIOS floppy stage-1 loader (80 cylinders, 2 heads, 18 sectors).
BITS 16
ORG 0x7C00

jmp short start
nop
db "X86MICRO"                 ; OEM name
dw 512                        ; bytes per sector
db 1                          ; sectors per cluster
dw 1                          ; reserved sectors (patched by image builder)
db 2                          ; FAT copies
dw 224                        ; root directory entries
dw 2880                       ; total sectors
db 0xF0                       ; 1.44-MB removable-media descriptor
dw 3                          ; sectors per FAT (sufficient for rescue area)
dw 18                         ; sectors per track
dw 2                          ; heads
dd 0                          ; hidden sectors
dd 0                          ; large total-sector field
db 0                          ; BIOS drive number
db 0
db 0x29                       ; extended boot signature
dd 0x36385852                 ; volume serial
db "X86 RESCUE "              ; volume label
db "FAT12   "                 ; filesystem label

MANIFEST_LBA       equ 1
MANIFEST_SEGMENT   equ 0x0800
STAGE2_SEGMENT     equ 0x1000
MANIFEST_MAGIC_0   equ 0x42363858
MANIFEST_MAGIC_1   equ 0x31544F4F
MAX_STAGE2_SECTORS equ 64

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

    mov eax, MANIFEST_LBA
    mov cx, 1
    mov dx, MANIFEST_SEGMENT
    xor bx, bx
    call read_chs
    jc disk_error

    mov ax, MANIFEST_SEGMENT
    mov es, ax
    cmp dword [es:0], MANIFEST_MAGIC_0
    jne manifest_error
    cmp dword [es:4], MANIFEST_MAGIC_1
    jne manifest_error
    cmp dword [es:8], 1
    jne manifest_error
    mov ecx, [es:20]
    test ecx, ecx
    jz manifest_error
    cmp ecx, MAX_STAGE2_SECTORS
    ja manifest_error
    mov eax, [es:16]
    add eax, MANIFEST_LBA
    mov dx, STAGE2_SEGMENT
    xor bx, bx
    call read_chs
    jc disk_error

    mov dl, [boot_drive]
    mov eax, MANIFEST_LBA
    jmp STAGE2_SEGMENT:0

disk_error:
    mov si, msg_disk
    jmp fatal
manifest_error:
    mov si, msg_manifest
fatal:
    call print
    cli
.hang:
    hlt
    jmp .hang

; EAX=LBA, CX=count, DX:BX=destination. Batch up to the end of the current
; track; no request crosses a track or 64-KiB DMA boundary.
read_chs:
    mov [next_lba], eax
    mov [remaining], cx
    mov es, dx
.next:
    cmp word [remaining], 0
    je .ok
    mov byte [retries], 3
.retry:
    mov eax, [next_lba]
    xor edx, edx
    mov ecx, 36
    div ecx
    cmp eax, 79
    ja .fail
    mov [cylinder], al
    mov eax, edx
    xor edx, edx
    mov ecx, 18
    div ecx
    mov [head], al
    mov [sector], dl
    mov eax, 18
    sub eax, edx
    cmp ax, [remaining]
    jbe .count_ready
    mov ax, [remaining]
.count_ready:
    mov [transfer_count], al
    mov dh, [head]
    mov cl, [sector]
    inc cl
    mov ch, [cylinder]
    mov dl, [boot_drive]
    mov al, [transfer_count]
    mov ah, 0x02
    int 0x13
    jnc .read
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    dec byte [retries]
    jnz .retry
.fail:
    stc
    ret
.read:
    movzx ax, byte [transfer_count]
    sub [remaining], ax
    movzx eax, ax
    add [next_lba], eax
    shl ax, 9
    add bx, ax
    jmp .next
.ok:
    clc
    ret

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp print
.done:
    ret

boot_drive db 0
retries db 0
cylinder db 0
head db 0
sector db 0
transfer_count db 0
remaining dw 0
next_lba dd 0
msg_disk db "Floppy read error", 13, 10, 0
msg_manifest db "Invalid floppy image", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
