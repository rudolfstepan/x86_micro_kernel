OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
MEMORY
{
    MYMEMORY : ORIGIN = 0x10000, LENGTH = 0x1000000
}

SECTIONS
{
    /* Set the starting address to the beginning of MYMEMORY */
    . = ORIGIN(MYMEMORY);

    /* Define the program header section */
    .header : {
        _header_start = .;                    /* Start of header */
        LONG(0x5250594D);                     /* 'MYPR' identifier in hex */
        LONG(0xDEADBEEF);                     /* Magic number placeholder */
        LONG(ADDR(.text));                    /* Entry point (start of .text) */
        LONG(SIZEOF(.text));                  /* Program size (size of .text) */
        LONG(ORIGIN(MYMEMORY));               /* Base address */
        LONG(_relocation_offset - _header_start); /* Relocation offset */
        LONG(SIZEOF(.relocation));            /* Relocation size */
        _header_end = .;
    } > MYMEMORY

    /* Place the program code in the .text section */
    .text : {
        _text_start = .;                      /* Define start of .text */
        *(.text)                              /* Include all .text contents */
        _text_end = .;                        /* Define end of .text */
    } > MYMEMORY

    /* Define the relocation section */
    .relocation : {
        _relocation_offset = .;
        *(.relocation)                        /* Include all .relocation data */
        _relocation_end = .;
    } > MYMEMORY
}
