section .multiboot
align 4
multiboot_header:

%ifdef USE_FRAMEBUFFER
    ; Multiboot header with framebuffer support
    MODULEALIGN equ 1<<0
    MEMINFO equ 1<<1
    VIDEO_MODE equ 1<<2
    FLAGS equ MODULEALIGN | MEMINFO | VIDEO_MODE
    MAGIC equ 0x1BADB002
    CHECKSUM equ -(MAGIC + FLAGS)
    
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    
    ; Header address fields (not used, set to 0)
    dd 0  ; header_addr
    dd 0  ; load_addr
    dd 0  ; load_end_addr
    dd 0  ; bss_end_addr
    dd 0  ; entry_addr
    
    ; Video mode fields
    dd 0            ; mode_type (0 = linear framebuffer)
    dd 1024         ; width
    dd 768          ; height
    dd 32           ; depth (bits per pixel)
%else
    ; Standard multiboot header (VGA text mode)
    MODULEALIGN equ 1<<0
    MEMINFO equ 1<<1
    FLAGS equ MODULEALIGN | MEMINFO
    MAGIC equ 0x1BADB002
    CHECKSUM equ -(MAGIC + FLAGS)
    
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
%endif

header_end:

; Flags
    ; - Bit 0: Align modules on page boundaries
    ; - Bit 1: Request memory information
    ; - Bit 2: Request framebuffer information

; MODULEALIGN equ (1 << 0)
; MEMINFO equ (1 << 1)
; FRAMEBUFFER equ (1 << 2)
; FLAGS equ (MODULEALIGN | MEMINFO | FRAMEBUFFER)
; CHECKSUM  equ -(0x1BADB002 + FLAGS)

; section .multiboot
; align 8
; multiboot_header:

;     dd 0x1BADB002                ; Multiboot magic number
;     dd FLAGS                    ; Flags (request framebuffer and memory info)
;     dd CHECKSUM                 ; Checksum (MAGIC + FLAGS + CHECKSUM = 0)

;     ; Optional fields for framebuffer request
; align 4
;     dd 1024                     ; Desired framebuffer width
;     dd 768                     ; Desired framebuffer height
;     dd 24                       ; Desired bits per pixel

; header_end: