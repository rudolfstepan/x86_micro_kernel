OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

MEMORY
{
  kernel_ram (rw) : ORIGIN = 0x00100000, LENGTH = 4M  /* Define a memory region for the kernel */
}

SECTIONS
{
  address = 0x00100000;  /* Load address of the kernel */

  /* Place the Multiboot2 header first */
  .multiboot_header address : {
    *(.multiboot_header)
  } > kernel_ram

  /* Place the code and read-only data sections */
  .text : {
    code = .;             /* Mark the start of the code section */
    *(.text)
    *(.rodata)
    . = ALIGN(4096);      /* Align the end of the text section */
  } > kernel_ram

  /* Place the initialized data section */
  .data : {
    data = .;             /* Mark the start of the data section */
    *(.data)
    . = ALIGN(4096);      /* Align the end of the data section */
  } > kernel_ram

  /* Place the BSS (uninitialized data) section */
  .bss : {
    bss = .;              /* Mark the start of the BSS section */
    *(.bss)
    . = ALIGN(4096);      /* Align the end of the BSS section */
  } > kernel_ram

  /* Define a section for the stack */
  .stack (NOLOAD) : {
    . = ALIGN(4096);      /* Align the start of the stack section */
    stack_start = .;      /* Mark the start of the stack */
    . += 8192;            /* Allocate 8KB for the stack */
    stack_end = .;        /* Mark the end of the stack */
  } > kernel_ram

  end_of_kernel = .;      /* Mark the end of the kernel */
}
