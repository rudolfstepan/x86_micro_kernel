section .multiboot
align 4
header_start:
    dd 0x1BADB002
    dd 0x00
    dd -(0x1BADB002 + 0x00)
    align 8
    ; Add other multiboot configurations here
header_end:
