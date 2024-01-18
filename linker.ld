/* linker.ld - Linker script */
OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
MEMORY
{
    MYMEMORY : ORIGIN = 0x10000, LENGTH = 0x100000
}

SECTIONS
{
    .text : {
        *(.text) /* Place the code (.text section) at the beginning of MYMEMORY */
    } > MYMEMORY
}
