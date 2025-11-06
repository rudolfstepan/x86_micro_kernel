OUTPUT_FORMAT("elf32-i386")
ENTRY(main)

MEMORY
{
    MYMEMORY : ORIGIN = 0x100000, LENGTH = 0x1000000 /* 0x00500000 is defined as user address space */
}

SECTIONS
{
    . = ORIGIN(MYMEMORY);

    /* Header Section (Optional for Custom Metadata) */
    .header : {
        _header_start = .;                     
        LONG(0x5250594D);                     
        LONG(0xDEADBEEF);                     
        LONG(_program_start - _header_start);    
        LONG(SIZEOF(.text));                 
        LONG(ORIGIN(MYMEMORY));              
        LONG(_relocation_offset - _header_start); 
        LONG(SIZEOF(.relocation));             
        _header_end = .;
    } > MYMEMORY

    /* Text Section (Code) */
    .text : {
        _program_start = .;        /* Mark the start of the program */
        *(.text.main)              /* Explicitly include main first */
        *(.text*)                  /* Include all other .text sections */
        . = ALIGN(4096);           /* Align to 4KB */
        _program_end = .;          /* Mark the end of the program */
    } > MYMEMORY

    /* Data Section (Initialized Data) */
    .data : {
        _data_start = .;           /* Mark the start of the .data section */
        *(.data)                   /* Include all initialized data */
        . = ALIGN(4096);           /* Align to 4KB */
        _data_end = .;             /* Mark the end of the .data section */
    } > MYMEMORY

    /* BSS Section (Uninitialized Data) */
    .bss : {
        _bss_start = .;            /* Mark the start of the .bss section */
        *(.bss)                    /* Include all uninitialized data */
        . = ALIGN(4096);           /* Align to 4KB */
        _bss_end = .;              /* Mark the end of the .bss section */
    } > MYMEMORY

    /* Stack Section (Optional) */
    .stack (NOLOAD) : {
        . = ALIGN(4096);           /* Align to 4KB */
        stack_start = .;           /* Mark the start of the stack */
        . += 8192;                 /* Allocate 8KB for the stack */
        stack_end = .;             /* Mark the end of the stack */
    } > MYMEMORY

    /* Relocation Section */
    .relocation : {
        _relocation_offset = .;
        *(.relocation)                        
        _relocation_end = .;
    } > MYMEMORY
}
