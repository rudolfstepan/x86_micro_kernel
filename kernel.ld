OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
address = 1M;  /* 0x00100000 */
SECTIONS
{
  .text address : AT(address) {
    code = .;
    *(.multiboot)
    *(.text)
    *(.rodata)
    . = ALIGN(8K);
  }
  .data : AT(address + (data - code))
  {
    data = .;
    *(.data)
    . = ALIGN(4096);
  }
  .bss : AT(address + (bss - code))
  {
    bss = .;
    *(.bss)
    . = ALIGN(4096);
  }
  .syscall_table : AT(address + (syscall_table - code))
  {
    syscall_table = .;
    *(.syscall_table)
    . = ALIGN(4096);
  }

  end_of_kernel = .;
}
