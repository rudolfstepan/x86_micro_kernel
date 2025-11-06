# x86 Microkernel - AI Coding Agent Instructions

## Project Overview
This is a bare-metal x86 microkernel written in C and assembly, booting via GRUB Multiboot. It provides a Linux-like shell environment with FAT32 filesystem support and custom executable format (`.PRG`).

**IMPORTANT**: The codebase follows a hierarchical OS structure with clear separation between architecture-specific code (`arch/x86/`), kernel core (`kernel/`), memory management (`mm/`), filesystems (`fs/`), drivers (`drivers/`), and libraries (`lib/`).

## Directory Structure Overview

```
arch/x86/          # x86-specific: boot, CPU (GDT/IDT/ISR/IRQ), paging
kernel/            # Core: init, syscall, proc, sched, time, shell
mm/                # Memory management (kmalloc)
fs/                # Filesystems: vfs, fat12, fat32
drivers/           # Drivers by type: block, char, video, net, bus
lib/libc/          # Userspace C library (stdio, stdlib, string)
userspace/bin/     # User programs
config/            # Linker scripts, GRUB config
scripts/           # Build and utility scripts
```

## Architecture Fundamentals

### Memory Layout (see `config/klink.ld`)
- **Kernel space**: `0x00100000` (1MB) - 16MB region for kernel code/data
- **User programs**: `0x01100000` - 8MB region for loaded executables
- **Stack**: 8KB allocated in kernel BSS section
- Memory is page-aligned (4KB boundaries) for all major sections

### Boot Sequence
1. GRUB loads Multiboot header (`arch/x86/boot/multiboot.asm`)
2. GDT setup (`arch/x86/cpu/gdt.asm`) establishes protected mode segments
3. IDT/ISR/IRQ handlers installed (`arch/x86/cpu/{idt,isr,irq}.asm`)
4. Syscall interrupt (INT 0x80) registered via `arch/x86/cpu/syscall.asm`
5. Kernel main (`kernel/init/kernel.c`) initializes drivers and command loop

### Custom Executable Format (.PRG)
User programs use a custom format instead of standard ELF:
- **Header structure** (`kernel/init/prg.h`): 12-byte header with magic `0x5250594D` ("MYPR")
- **Linker script** (`config/cli.ld`): Programs linked at `0x100000` with custom header section
- **Entry point**: Header specifies entry address and relocation info
- **Loading**: `load_and_relocate_program()` in `kernel/init/prg.c` handles position-independent loading

## Build System

### Primary Commands (Makefile)
```bash
make all      # Full build: compile → link → create ISO
make run      # Launch QEMU with built kernel
make clean    # Remove build artifacts
make help     # Show build system help
```

### Build Pipeline
1. **Bootloader**: NASM assembles `.asm` → ELF32 objects in `build/arch/boot/`
2. **CPU Management**: x86 GDT, IDT, ISR, IRQ compiled to `build/arch/cpu/`
3. **Kernel Core**: Init, syscall, proc, sched, time, shell → `build/kernel/`
4. **Memory Management**: kmalloc compiled to `build/mm/`
5. **Filesystems**: VFS, FAT12, FAT32 → `build/fs/`
6. **Drivers**: Block, char, video, net, bus → `build/drivers/`
7. **Libraries**: libc compiled to `build/lib/`
8. **Final link**: Uses `config/klink.ld` to place Multiboot header in first 8KB
9. **ISO creation**: GRUB bootloader + kernel packaged as `kernel.iso`

### Critical Compiler Flags
- `-m32`: Force 32-bit x86 code generation
- `-ffreestanding`: No standard library, bare-metal environment
- `-nostdlib -nodefaultlibs`: Disable all standard libraries
- `-fno-builtin`: Prevent GCC intrinsics (e.g., optimized `memcpy`)
- `-I.`: Project root for relative includes
- `-Iarch/x86/include`: Architecture headers
- `-Iinclude`: Public API headers
- `-Ilib/libc`: C library headers

## Syscall Convention

### Mechanism
User programs trigger `INT 0x80`, passing syscall index in `EAX`, args in `EBX/ECX/EDX`:
```c
// Example syscall invocation
asm("mov %0, %%eax; int $0x80" : : "r"(syscall_number));
```

### Syscall Table (`kernel/init/kernel.c`)
Index-based dispatch to kernel functions:
```c
void* syscall_table[512] = {
    (void*)&vga_write_char,      // 0
    (void*)&kernel_print_number, // 1
    (void*)&pit_delay,           // 2
    // ... see arch/x86/include/sys.h for constants
};
```

**Adding syscalls**: 
1. Add function pointer to `syscall_table[]` in `kernel/init/kernel.c`
2. Define constant in `arch/x86/include/sys.h` (e.g., `SYS_MALLOC`)
3. Add case in `syscall_handler()` switch statement with correct argument count

## Custom Toolchain (libc)

### Purpose
Minimal C library (`lib/libc/`) providing standard functions for user programs:
- `stdio.h`: `printf()`, `getchar()` → syscalls to VGA/keyboard drivers
- `stdlib.h`: `malloc()`/`free()` → syscalls to kernel memory manager
- `string.h`: `strlen()`, `strcpy()`, etc. → reimplemented without libc

### Usage in User Programs
```c
#include <lib/libc/stdio.h>  // Include from project root
int main() {
    printf("Hello from userspace\n");  // Syscalls to kernel
}
```

**Linking**: User programs must link against compiled libc objects

## Filesystem Architecture

### Driver Stack
1. **ATA/IDE driver** (`drivers/block/ata.c`): Raw sector read/write to disk
2. **Filesystem layer** (`fs/fat32/`): Parses FAT32 structures
   - `fat32.c`: Boot sector parsing, FAT table access
   - `fat32_dir.c`: Directory traversal
   - `fat32_files.c`: File read/write operations
   - `fat32_cluster.c`: Cluster chain following

### Key Functions
- `fat32_mount()`: Read boot sector, validate FAT32
- `fat32_list_directory()`: Read directory entries from cluster chain
- `fat32_read_file()`: Follow cluster chain, read file data
- Global state: `current_directory_cluster` tracks CWD

### Shell Commands (`kernel/shell/command.c`)
Command table dispatches to handler functions:
```c
command_t command_table[] = {
    {"ls", cmd_ls},      // Uses fat32_list_directory()
    {"cd", cmd_cd},      // Updates current_directory_cluster
    {"run", cmd_run},    // Loads .PRG and executes
    // ...
};
```

## Interrupt Handling

### Structure
- **ISRs** (`arch/x86/cpu/isr.asm`): CPU exceptions (0-31)
- **IRQs** (`arch/x86/cpu/irq.asm`): Hardware interrupts (32-47), remapped from PIC
- **Handlers**: C functions registered via `register_interrupt_handler(irq, handler)`

### Example: Timer (PIT)
```c
// kernel/time/pit.c
register_interrupt_handler(0, timer_callback);  // IRQ 0 = timer
```

## Development Workflow

### Environment
- **Linux/WSL2 required** for build tools (NASM, i686-elf-gcc, GRUB utilities)
- **QEMU** for testing: `qemu-system-i386 -cdrom kernel.iso`
- Scripts must be executable: `chmod +x scripts/*.sh`

### Debugging
- `dump <addr> <end>`: Hex dump memory range via shell
- `mem`: Display kernel heap allocation stats
- QEMU with GDB: Add `-s -S` to `scripts/run.sh`, connect GDB to `:1234`

### Adding Features
1. **New driver**: Create in `drivers/<type>/<name>/`, Makefile auto-detects
2. **New syscall**: Add to `kernel/init/kernel.c` table + switch statement
3. **New shell command**: Add to `command_table[]` in `kernel/shell/command.c`
4. **New kernel module**: Create in `kernel/<module_name>/` with sources
5. **User program**: Write in `userspace/bin/`, link with `config/cli.ld`

## Common Pitfalls

1. **Linker script order matters**: Multiboot header MUST be first in binary (see `config/klink.ld`)
2. **No floating point**: FPU not initialized; use integer math only
3. **Stack size limited**: 8KB kernel stack; deep recursion will crash
4. **Memory alignment**: GCC may generate unaligned accesses; use `__attribute__((packed))` for hardware structures
5. **Interrupts during boot**: CLI/STI state critical; bootloader disables, kernel re-enables after IDT setup
6. **Include paths**: Use relative from project root (e.g., `"arch/x86/sys.h"` not `"sys.h"`)

## Testing Changes
```bash
make clean && make all  # Full rebuild
make run                # Boot in QEMU
# At shell prompt:
help                    # List commands
ls /                    # Test FAT32 driver
```
