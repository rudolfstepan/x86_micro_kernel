OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5);
}

SECTIONS
{
  . = 0x00400000 + SIZEOF_HEADERS;
  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
  } :text

  /DISCARD/ : {
    *(.data .data.*)
    *(.bss .bss.*)
    *(.comment)
    *(.note .note.*)
    *(.eh_frame .eh_frame.*)
  }

  _child_end = .;
  ASSERT(_child_end <= 0x00401000,
         "x86_64 child exceeds its fixed compact RX page")
}
