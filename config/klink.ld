OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

MEMORY
{
  kernel_ram (rw) : ORIGIN = 0x00100000, LENGTH = 32M  /* Kernel memory region, 32 MB (increased for network drivers) */
  user_programs (rw) : ORIGIN = 0x02100000, LENGTH = 8M  /* User programs memory region, 8 MB */
}

SECTIONS
{
  /* Multiboot header (must be within the first 8 KB of the kernel binary) */
  .multiboot 0x00100000 : ALIGN(8) {
    *(.multiboot)
  } > kernel_ram

  /* Kernel code and read-only data */
  .text ALIGN(4096) : {
    _kernel_start = .;
    *(.text)
    *(.rodata)
    _kernel_text_end = .;
  } > kernel_ram

  /* Kernel initialized data */
  .data ALIGN(4096) : {
    _kernel_data_start = .;
    *(.data)
    _kernel_data_end = .;
  } > kernel_ram

  /* Kernel uninitialized data (BSS) */
  .bss ALIGN(4096) : {
    _kernel_bss_start = .;
    *(.bss)
    *(COMMON)
    _kernel_bss_end = .;
  } > kernel_ram

  /* Stack section (8 KB) */
  .stack ALIGN(4096) (NOLOAD) : {
    _stack_start = .;
    . += 8192;  /* Allocate 8 KB for the stack */
    _stack_end = .;
  } > kernel_ram

  /* User program code */
  .user_text ALIGN(4096) : {
    user_code = .;
    *(.user_text)
    *(.user_rodata)
  } > user_programs

  /* User program initialized data */
  .user_data ALIGN(4096) : {
    user_data = .;
    *(.user_data)
  } > user_programs

  /* User program uninitialized data (BSS) */
  .user_bss ALIGN(4096) : {
    user_bss = .;
    *(.user_bss)
  } > user_programs

  _kernel_end = .;

  /* Ensure kernel fits within kernel_ram */
  ASSERT(_kernel_end <= ORIGIN(kernel_ram) + LENGTH(kernel_ram), "Kernel exceeds kernel_ram region!")
}
