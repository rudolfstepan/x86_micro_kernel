OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x00400000;
  .text ALIGN(4096) : {
    *(.text)
  } :text

  . = ALIGN(4096);
  .data : {
    *(.data)
  } :data

  .bss (NOLOAD) : {
    *(.bss)
  } :data

  _probe_end = .;
  ASSERT(_probe_end <= 0x00408000,
         "x86_64 ELF64 probe exceeds its fixed eight-page user window")
}
