OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

MEMORY
{
  /* Existing BIOS ELF32 PT_LOAD envelope: [1 MiB, 64 MiB).
     This is an admission ceiling, not an eager reservation of 63 MiB.
     The PMM reserves the actual image/guard/stack through _stack_end. */
  kernel_ram (rw) : ORIGIN = 0x00100000, LENGTH = 63M
}

SECTIONS
{
  /* Multiboot header (must be within the first 8 KB of the kernel binary) */
  .multiboot 0x00100000 : ALIGN(8) {
    *(.multiboot)
  } > kernel_ram

  /* Runtime-visible content identifier emitted by GNU ld and LLD. */
  .note.gnu.build-id ALIGN(4) : {
    _kernel_build_id_note_start = .;
    KEEP(*(.note.gnu.build-id))
    _kernel_build_id_note_end = .;
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

  /* Ring-3 executables are separately loaded into private address spaces.
     Do not silently revive the obsolete embedded-user area at 33 MiB. */
  .legacy_user : {
    *(.user_text .user_text.* .user_rodata .user_rodata.*)
    *(.user_data .user_data.* .user_bss .user_bss.*)
  } > kernel_ram
  ASSERT(SIZEOF(.legacy_user) == 0, "Embedded userspace sections are not supported")

  /* Boot/rescue kernel stack: full guard page + 8 KiB stack. */
  .stack ALIGN(4096) (NOLOAD) : {
    _stack_guard_start = .;
    . += 4096;
    _stack_guard_end = .;
    _stack_start = .;
    . += 8192;  /* Allocate 8 KB for the stack */
    _stack_end = .;
  } > kernel_ram

  _kernel_end = _stack_end;

  /* Ensure kernel fits within kernel_ram */
  ASSERT(_kernel_end <= ORIGIN(kernel_ram) + LENGTH(kernel_ram), "Kernel exceeds kernel_ram region!")
  ASSERT(ORIGIN(kernel_ram) + LENGTH(kernel_ram) == 0x04000000, "Native ELF32 loader limit changed")
  ASSERT(_stack_guard_end - _stack_guard_start == 4096, "Boot stack guard must remain one page")
  ASSERT(_stack_end - _stack_start == 8192, "Boot stack capacity changed")
}
