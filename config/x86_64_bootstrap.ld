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
    *(.text .text.*)
    . = ALIGN(4096);
    _text_end = .;
  }

  .rodata ALIGN(4096) : {
    _rodata_start = .;
    *(.rodata .rodata.*)
    . = ALIGN(4096);
    _rodata_end = .;
  }

  .data ALIGN(4096) : {
    _data_start = .;
    *(.data .data.*)
    . = ALIGN(4096);
    _data_end = .;
  }

  .bss ALIGN(4096) (NOLOAD) : {
    _bss_start = .;
    *(COMMON)
    *(.bss .bss.*)
    . = ALIGN(4096);
    _bss_end = .;
  }

  ASSERT(_bss_end <= 0x00162000,
         "x86_64 assembly bootstrap overlaps the fixed C bridge")

  .c_core_bridge 0x00162000 : {
    _c_core_bridge_start = .;
    KEEP(*(.c_core_bridge))
    _c_core_bridge_end = .;
  }

  .c_core_text 0x00163000 : {
    _c_core_text_start = .;
    KEEP(*(.c_core_text))
    _c_core_text_end = .;
  }

  .c_core_rodata 0x00164000 : {
    _c_core_rodata_start = .;
    KEEP(*(.c_core_rodata))
    _c_core_rodata_end = .;
  }

  .c_core_data 0x00165000 : {
    _c_core_data_start = .;
    KEEP(*(.c_core_data))
    _c_core_data_end = .;
  }

  .c_core_bss 0x00166000 (NOLOAD) : {
    _c_core_bss_start = .;
    KEEP(*(.c_core_bss))
    _c_core_bss_end = .;
  }

  _x86_64_bootstrap_end = .;

  .init_array : { *(.init_array .init_array.*) }
  .fini_array : { *(.fini_array .fini_array.*) }
  .eh_frame : { *(.eh_frame .eh_frame.*) }
  .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) }

  /DISCARD/ : {
    *(.comment)
    *(.note .note.*)
  }

  ASSERT(SIZEOF(.init_array) == 0, "x86_64 C core contains constructors")
  ASSERT(SIZEOF(.fini_array) == 0, "x86_64 C core contains destructors")
  ASSERT(SIZEOF(.eh_frame) == 0, "x86_64 C core contains unwind metadata")
  ASSERT(SIZEOF(.gcc_except_table) == 0, "x86_64 C core contains exception metadata")

  ASSERT(SIZEOF(.c_core_bridge) > 0 && SIZEOF(.c_core_bridge) <= 4096,
         "x86_64 C serial bridge exceeds one RX page")
  ASSERT(SIZEOF(.c_core_text) > 0 && SIZEOF(.c_core_text) <= 4096,
         "x86_64 C text exceeds one RX page")
  ASSERT(SIZEOF(.c_core_rodata) > 0 && SIZEOF(.c_core_rodata) <= 4096,
         "x86_64 C rodata exceeds one R/NX page")
  ASSERT(SIZEOF(.c_core_data) == 32,
         "x86_64 C initialized data ABI changed")
  ASSERT(SIZEOF(.c_core_bss) == 160,
         "x86_64 C BSS or handoff ABI changed")

  ASSERT(_x86_64_bootstrap_end <= 0x00200000,
         "x86_64 bootstrap exceeds its single 2 MiB identity map")
}
