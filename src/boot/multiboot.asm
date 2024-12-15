; section .multiboot
; align 8
; multiboot_header:

; MODULEALIGN equ 1<<0
; MEMINFO equ 1<<1
; FLAGS equ MODULEALIGN | MEMINFO
; MAGIC equ 0x1BADB002
; CHECKSUM equ -(MAGIC + FLAGS)


;   dd MAGIC
;   dd FLAGS
;   dd CHECKSUM

; header_end:

; Flags
;     - Bit 0: Align modules on page boundaries
;     - Bit 1: Request memory information
;     - Bit 2: Request framebuffer information

; MODULEALIGN equ (1 << 0)
; MEMINFO equ (1 << 1)
; FRAMEBUFFER equ (1 << 2)
; FLAGS equ (MODULEALIGN | MEMINFO | FRAMEBUFFER)
; CHECKSUM  equ -(0x1BADB002 + FLAGS)

global start

section .multiboot
align 8
header_start:
    ; Multiboot 2 mandatory header
    dd 0xe85250d6            ; Multiboot 2 magic number
    dd 0                      ; Architecture (0 for i386)
    dd header_end - header_start ; Total header length
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; Checksum

    ; Information Request Tag
    align 8
    dw 1                      ; Tag type: Information request
    dw 0                      ; Reserved (must be zero)
    dd 16                     ; Size of this tag (16 bytes total)
    dd 6                      ; Request memory map
    dd 5                      ; Request boot device

    ; Framebuffer Tag
    align 8
    dw 5                      ; Tag type: Framebuffer
    dw 0                      ; Reserved (must be zero)
    dd 24                     ; Size of this tag (24 bytes total)
    dd 800                   ; Desired framebuffer width
    dd 600                    ; Desired framebuffer height
    dd 32                     ; Desired bits per pixel

    ; End Tag
    align 8
    dw 0                      ; Tag type: End
    dw 0                      ; Reserved (must be zero)
    dd 8                      ; Size of this tag (8 bytes total)

header_end:
