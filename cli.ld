OUTPUT_FORMAT("elf32-i386")
ENTRY(main)
MEMORY
{
    MYMEMORY : ORIGIN = 0x00500000, LENGTH = 0x1000000 /* 0x00500000 is defined as user address space */
}

SECTIONS
{
    . = ORIGIN(MYMEMORY);

    .header : {
        _header_start = .;                     
        LONG(0x5250594D);                     
        LONG(0xDEADBEEF);                     
        LONG(_text_start - _header_start);    
        LONG(SIZEOF(.text));                 
        LONG(ORIGIN(MYMEMORY));              
        LONG(_relocation_offset - _header_start); 
        LONG(SIZEOF(.relocation));             
        _header_end = .;
    } > MYMEMORY

    .text : {
        _text_start = .;                      
        *(.text.main)      /* Explicitly include main first */
        *(.text*)          /* Include all other .text sections */
        _text_end = .;                        
    } > MYMEMORY

    .relocation : {
        _relocation_offset = .;
        *(.relocation)                        
        _relocation_end = .;
    } > MYMEMORY
}
