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

  .data : {
    *(.data .data.*)
  }

  .bss (NOLOAD) : {
    *(.bss .bss.*)
  }

  .init_array : { *(.init_array .init_array.*) }
  .fini_array : { *(.fini_array .fini_array.*) }
  .eh_frame : { *(.eh_frame .eh_frame.*) }

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
  }

  _shell_end = .;
  ASSERT(SIZEOF(.init_array) == 0, "x86_64 shell contains constructors")
  ASSERT(SIZEOF(.fini_array) == 0, "x86_64 shell contains destructors")
  ASSERT(SIZEOF(.eh_frame) == 0, "x86_64 shell contains unwind metadata")
  ASSERT(SIZEOF(.data) == 0, "x86_64 shell contains writable data")
  ASSERT(SIZEOF(.bss) == 0, "x86_64 shell contains writable BSS")
  ASSERT(_shell_end <= 0x00401000,
         "x86_64 shell exceeds its fixed compact RX page")
}
