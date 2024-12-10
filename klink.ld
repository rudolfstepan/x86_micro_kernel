OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
SECTIONS {
    /* Multiboot Header */
    . = 0x00008000;
    .multiboot : {
        *(.multiboot)
    }

    /* Kernel Text Section */
    . = 0x00100000;
    .text : {
        *(.text)
    }

    /* Kernel Data Section */
    .data : {
        *(.data)
    }

    /* Kernel BSS Section */
    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Kernel Stack */
    . = ALIGN(16); /* Stelle sicher, dass die Adresse 16-Byte-aligned ist */
    .stack : {
        __stack_bottom = .; /* Markiere den Beginn des Stacks */
        . += 0x10000;       /* 64 KB Stack */
        __stack_top = .;    /* Markiere das Ende des Stacks */
    }

    /* Kernel Heap */
    . = ALIGN(16);
    .heap : {
        __heap_start = .;   /* Start des Heaps */
        . += 0x1F00000;     /* 31 MB Heap */
        __heap_end = .;     /* Ende des Heaps */
    }
}
