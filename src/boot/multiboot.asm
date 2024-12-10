section .multiboot
align 8
header_start:
    ; Multiboot 2 mandatory header
    dd 0xe85250d6            ; Multiboot 2 magic number
    dd 0                      ; Architecture (0 for i386)
    dd header_end - header_start ; Total header length
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; Checksum

mb2_tag_info_start:
        dw 1                                    ; multiboot information request
        dw 0
        dd mb2_tag_info_end - mb2_tag_info_start
        dd 1
        ;dd 5
        dd 6
mb2_tag_info_end:

;         align 8
; mb2_tag_console_start:
;         dw 4                                    ; console flags
;         dw 0
;         dd mb2_tag_console_end - mb2_tag_console_start
;         dd 0x3
; mb2_tag_console_end:

        align 8
mb2_tag_fb_start:
        dw 5                                    ; framebuffer settings
        dw 1
        dd mb2_tag_fb_end - mb2_tag_fb_start
        dd 800
        dd 600
        dd 32
mb2_tag_fb_end:

        align 8
mb2_tag_end_start:
        dw 0                                    ; last tag
        dw 0
        dd mb2_tag_end_end - mb2_tag_end_start
mb2_tag_end_end:

header_end:

[BITS 32]
global start
global interrupt

extern __stack_top
extern kernel_main

section .text
start:
    ; Initialize stack pointer hadcoded to 0x1FDFFFFF which is the end of the 512MB memory
    mov esp, __stack_top

    ; Call the kernel main function
    push ebx
    push dword 0x36d76289

    call kernel_main
    ; Halt the system if kernel_main returns
    cli

.halt:
    hlt
    jmp .halt