OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PROGRAM_BASE = 0x02100000;
PROGRAM_HEADER_SIZE = 28;
PROGRAM_LIMIT = 0x00800000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = PROGRAM_BASE + PROGRAM_HEADER_SIZE;

    .text :
    {
        KEEP(*(.text._start))
        *(.text .text.*)
    } :text

    .rodata :
    {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(4096);
    .data :
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    __program_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.llvm_addrsig)
    }
}

ASSERT(__program_end - PROGRAM_BASE <= PROGRAM_LIMIT,
       "user program exceeds the 8 MiB loader region")
