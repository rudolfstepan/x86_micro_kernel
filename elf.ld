OUTPUT_FORMAT("elf32-i386")
ENTRY(start)
MEMORY
{
    MYMEMORY : ORIGIN = 0x10000, LENGTH = 0x1000000
}

SECTIONS
{
    /* Place the text (code) section at the beginning of MYMEMORY */
    .text : {
        _text_start = .;                /* Define start of .text section */
        *(.text)                        /* Include all .text section contents */
        _text_end = .;                  /* Define end of .text section */
    } > MYMEMORY

    /* Place the data section */
    .data : {
        _data_start = .;                /* Define start of .data section */
        *(.data)                        /* Include all .data section contents */
        _data_end = .;                  /* Define end of .data section */
    } > MYMEMORY

    /* Place the bss (uninitialized data) section */
    .bss : {
        _bss_start = .;                 /* Define start of .bss section */
        *(.bss)                         /* Include all .bss section contents */
        _bss_end = .;                   /* Define end of .bss section */
    } > MYMEMORY

    /* Define symbols for the entry point and program size */
    _program_start = _text_start;       /* Program start address */
    _program_end = _bss_end;            /* Program end address (after .bss) */
    _program_size = _program_end - _program_start; /* Total program size */
}
