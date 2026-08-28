OUTPUT_FORMAT(elf32-i386)
ENTRY(x86_64_bootstrap_start)

SECTIONS
{
  . = 0x00100000;

  .multiboot ALIGN(4) : {
    KEEP(*(.multiboot))
  }

  .text ALIGN(4096) : {
    _text_start = .;
    *(.text)
    . = ALIGN(4096);
    _text_end = .;
  }

  .rodata ALIGN(4096) : {
    _rodata_start = .;
    *(.rodata)
    . = ALIGN(4096);
    _rodata_end = .;
  }

  .data ALIGN(4096) : {
    _data_start = .;
    *(.data)
    . = ALIGN(4096);
    _data_end = .;
  }

  .bss ALIGN(4096) (NOLOAD) : {
    _bss_start = .;
    *(COMMON)
    *(.bss)
    . = ALIGN(4096);
    _bss_end = .;
  }

  _x86_64_bootstrap_end = .;

  ASSERT(_x86_64_bootstrap_end <= 0x00200000,
         "x86_64 bootstrap exceeds its single 2 MiB identity map")
}
