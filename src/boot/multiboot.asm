section .multiboot
align 8
multiboot_header:
;     dd 0xe85250d6                ; Magic number for Multiboot2
;     dd 0                         ; Architecture (0 = i386)
;     dd header_end - multiboot_header ; Total header length
;     dd -(0xe85250d6 + 0 + (header_end - multiboot_header)) ; Checksum

;     ; Information request tag (request memory map and other info)
;     align 8
;     dw 1                          ; Tag type (information request)
;     dw 0                          ; Flags
;     dd info_request_end - info_request ; Size of the tag
; info_request:
;     dd 6                          ; Request memory map
;     dd 4                          ; Request boot loader name
;     align 8
; info_request_end:

;     ; End tag
;     align 8
;     dw 0                          ; Tag type (end tag)
;     dw 0
;     dd 8                          ; Size of the end tag


MODULEALIGN equ 1<<0
MEMINFO equ 1<<1
FLAGS equ MODULEALIGN | MEMINFO
MAGIC equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)


  dd MAGIC
  dd FLAGS
  dd CHECKSUM


header_end:
