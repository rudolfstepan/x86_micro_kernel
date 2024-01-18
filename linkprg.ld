OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
MEMORY
{
    MYMEMORY : ORIGIN = 0x10000, LENGTH = 0x1000000
}

SECTIONS
{
    . = ORIGIN(MYMEMORY);

    .header : {
        LONG(0x5250594D); /* 'MYPR' in ASCII, represented as a hex value */
        SHORT(1);         /* Version number */
        LONG(ADDR(.text));/* Entry point (address of .text section) */
        LONG(_textSize);  /*  Size of the text section */
    } > MYMEMORY

    .text : {
        *(.text) /* Place the code (.text section) */
        _textSize = SIZEOF(.text);
    } > MYMEMORY

    /* Define other sections like .data and .bss as needed */
}
