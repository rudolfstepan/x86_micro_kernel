OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

MEMORY
{
  kernel_ram (rw) : ORIGIN = 0x00100000, LENGTH = 4M  /* Define a memory region for the kernel */
  user_programs (rw) : ORIGIN = 0x00500000, LENGTH = 8M  /* Define a memory region for user programs */
}

SECTIONS
{
  address = 0x00100000;  /* Load address of the kernel */

  /* Place the Multiboot2 header first */
  .multiboot address : {
    *(.multiboot)
  } > kernel_ram

  /* Place the code and read-only data sections */
  .text : {
    _kernel_start = .;    /* Mark the start of the kernel */
    *(.text)              /* Kernel code */
    *(.rodata)            /* Read-only data */
    . = ALIGN(4096);      /* Align the end of the text section */
    _kernel_text_end = .; /* Mark the end of the text section */
  } > kernel_ram

  /* Place the initialized data section */
  .data : {
    _kernel_data_start = .; /* Mark the start of the data section */
    *(.data)                /* Initialized data */
    . = ALIGN(4096);        /* Align the end of the data section */
    _kernel_data_end = .;   /* Mark the end of the data section */
  } > kernel_ram

  /* Place the syscall table section */
  .syscall_table : {
    syscall_table_start = .;  /* Mark the start of the syscall table section */
    *(.syscall_table)         /* Include all items in the .syscall_table section */
    . = ALIGN(4096);          /* Align the end of the syscall table section */
    syscall_table_end = .;    /* Mark the end of the syscall table */
  } > kernel_ram

  /* Place the BSS (uninitialized data) section */
  .bss : {
    _kernel_bss_start = .; /* Mark the start of the BSS section */
    *(.bss)                /* Uninitialized data */
    . = ALIGN(4096);       /* Align the end of the BSS section */
    _kernel_bss_end = .;   /* Mark the end of the BSS section */
  } > kernel_ram

  /* Define a section for the stack */
  .stack (NOLOAD) : {
    . = ALIGN(4096);      /* Align the start of the stack section */
    stack_start = .;      /* Mark the start of the stack */
    . += 8192;            /* Allocate 8KB for the stack */
    stack_end = .;        /* Mark the end of the stack */
  } > kernel_ram

  /* Place user program code in a separate section */
  .user_text : {
    user_code = .;        /* Mark the start of the user code section */
    *(.user_text)         /* Include user code */
    *(.user_rodata)       /* Include user read-only data */
    . = ALIGN(4096);      /* Align the end of the user code section */
  } > user_programs

  /* Place user program data */
  .user_data : {
    user_data = .;        /* Mark the start of the user data section */
    *(.user_data)         /* Include user data */
    . = ALIGN(4096);      /* Align the end of the user data section */
  } > user_programs

  /* Place user program BSS */
  .user_bss : {
    user_bss = .;         /* Mark the start of the user BSS section */
    *(.user_bss)          /* Include user uninitialized data */
    . = ALIGN(4096);      /* Align the end of the user BSS section */
  } > user_programs

  /* Mark the end of the kernel */
  _kernel_end = .;        /* Mark the end of the kernel */
}
