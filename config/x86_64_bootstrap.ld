OUTPUT_FORMAT(elf32-i386)
ENTRY(x86_64_bootstrap_start)

SECTIONS
{
  . = 0x00100000;

  .multiboot ALIGN(4) : {
    KEEP(*(.multiboot))
  }

  .text ALIGN(4096) : {
    *(.text)
  }

  .rodata ALIGN(16) : {
    *(.rodata)
  }

  .data ALIGN(16) : {
    *(.data)
  }

  .bss ALIGN(4096) (NOLOAD) : {
    *(COMMON)
    *(.bss)
  }

  _x86_64_bootstrap_end = .;

  ASSERT(_x86_64_bootstrap_end <= 0x00200000,
         "x86_64 bootstrap exceeds its single 2 MiB identity map")
}
