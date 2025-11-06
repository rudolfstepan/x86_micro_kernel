section .multiboot
align 8
multiboot_header:

MODULEALIGN equ 1<<0
MEMINFO equ 1<<1
FLAGS equ MODULEALIGN | MEMINFO
MAGIC equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)


  dd MAGIC
  dd FLAGS
  dd CHECKSUM


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