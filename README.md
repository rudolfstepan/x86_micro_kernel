# x86 Microkernel - Reorganized Structure

## Overview
This document describes the reorganized codebase structure following standard operating system design patterns.

## Directory Structure

```
x86_micro_kernel/
├── arch/                    # Architecture-specific code
│   └── x86/                 # x86 (32-bit) implementation
│       ├── boot/            # Boot sequence and initialization
│       │   ├── multiboot.asm    # Multiboot header and entry
│       │   ├── bootloader.asm   # Early boot code
│       │   └── stack.asm        # Stack setup
│       ├── cpu/             # CPU management
│       │   ├── gdt.{asm,c}      # Global Descriptor Table
│       │   ├── idt.{asm,c}      # Interrupt Descriptor Table
│       │   ├── isr.{asm,c}      # Interrupt Service Routines
│       │   ├── irq.{asm,c}      # Hardware Interrupt Requests
│       │   └── syscall.asm      # System call entry point
│       ├── mm/              # Architecture-specific memory management
│       │   └── paging.{c,h}     # x86 paging implementation
│       └── include/         # x86-specific headers
│           ├── sys.h            # System structures (Registers, etc.)
│           └── mbheader.h       # Multiboot structures
│
├── kernel/                  # Core kernel functionality
│   ├── init/                # Kernel initialization
│   │   ├── kernel.c             # Main kernel entry and setup
│   │   └── prg.{c,h}            # PRG executable loader
│   ├── syscall/             # System call handling
│   │   ├── syscall.c            # Syscall dispatcher
│   │   └── syscall_table.c      # Syscall table definitions
│   ├── proc/                # Process management
│   │   └── process.{c,h}        # Process creation/destruction
│   ├── sched/               # Scheduler
│   │   ├── scheduler.{c,h}      # Task scheduling
│   │   └── switch.asm           # Context switching
│   ├── time/                # Timers and timing
│   │   ├── pit.{c,h}            # Programmable Interval Timer
│   │   ├── hpet.{c,h}           # High Precision Event Timer
│   │   └── apic.{c,h}           # Advanced PIC
│   └── shell/               # Built-in command shell
│       └── command.{c,h}        # Command parser and handlers
│
├── mm/                      # Memory management (arch-independent)
│   └── kmalloc.{c,h}        # Kernel memory allocator (malloc/free)
│
├── fs/                      # Filesystem layer
│   ├── vfs/                 # Virtual Filesystem (abstraction layer)
│   │   └── filesystem.{c,h}     # VFS interface
│   ├── fat12/               # FAT12 implementation
│   │   └── fat12.{c,h}          # FAT12 driver
│   └── fat32/               # FAT32 implementation
│       ├── fat32.{c,h}          # FAT32 core
│       ├── fat32_cluster.c      # Cluster chain management
│       ├── fat32_dir.c          # Directory operations
│       └── fat32_files.c        # File operations
│
├── drivers/                 # Device drivers
│   ├── block/               # Block devices
│   │   ├── ata.{c,h}            # ATA/IDE hard disk driver
│   │   └── fdd.{c,h}            # Floppy disk driver
│   ├── char/                # Character devices
│   │   ├── kb.{c,h}             # Keyboard driver
│   │   ├── rtc.{c,h}            # Real-Time Clock
│   │   └── io.{c,h}             # Port I/O utilities
│   ├── video/               # Video drivers
│   │   └── video.{c,h}          # VGA text mode driver
│   ├── net/                 # Network drivers
│   │   ├── ne2000.{c,h}         # NE2000 network card
│   │   ├── rtl8139.{c,h}        # Realtek RTL8139
│   │   ├── e1000.{c,h}          # Intel E1000
│   │   ├── vmxnet3.{c,h}        # VMware vmxnet3
│   │   └── ethernet.{c,h}       # Ethernet protocol
│   └── bus/                 # Bus drivers
│       ├── pci.{c,h}            # PCI bus enumeration
│       └── drives.h             # Drive management structures
│
├── lib/                     # Libraries
│   ├── libc/                # C standard library (for userspace)
│   │   ├── stdio.{c,h}          # Standard I/O (printf, etc.)
│   │   ├── stdlib.{c,h}         # Standard library (malloc, etc.)
│   │   ├── string.{c,h}         # String functions
│   │   ├── definitions.h        # Common type definitions
│   │   └── trycatch.asm         # Exception handling
│   └── libk/                # Kernel utility library
│       └── (kernel-only utilities)
│
├── include/                 # Public header files (mirrors source structure)
│   ├── arch/x86/            # Architecture headers
│   ├── kernel/              # Kernel headers
│   ├── mm/                  # Memory management headers
│   ├── fs/                  # Filesystem headers
│   ├── drivers/             # Driver headers
│   └── lib/                 # Library headers
│
├── userspace/               # User programs
│   ├── bin/                 # User binaries
│   │   ├── date.c               # Date display program
│   │   ├── dir.c                # Directory listing program
│   │   └── test.c               # Test program
│   └── include/             # Userspace application headers
│
├── scripts/                 # Build and utility scripts
│   ├── reorganize.ps1           # File reorganization script
│   ├── update_includes.ps1      # Include path updater
│   ├── update_includes_phase2.ps1
│   ├── create_includes.ps1      # Include directory generator
│   ├── make_image.sh            # Disk image creation
│   ├── build.sh                 # Build script
│   └── run.sh                   # QEMU launcher
│
├── config/                  # Configuration files
│   ├── klink.ld             # Kernel linker script
│   ├── cli.ld               # User program linker script
│   └── grub.cfg             # GRUB bootloader configuration
│
├── build/                   # Build output (generated)
│   ├── arch/                # Architecture object files
│   ├── kernel/              # Kernel object files
│   ├── mm/                  # Memory management objects
│   ├── fs/                  # Filesystem objects
│   ├── drivers/             # Driver objects
│   ├── lib/                 # Library objects
│   └── kernel.bin           # Final kernel binary
│
├── iso/                     # ISO build directory (generated)
│   └── boot/                # Bootable ISO contents
│
├── Makefile                 # Build system
├── Makefile.old             # Original Makefile (backup)
├── README.md                # This file
└── README-ORIGINAL.md       # Original README

```

## Build System

### Quick Start
```bash
make clean      # Clean build artifacts
make all        # Build kernel and create ISO
make run        # Build and run in QEMU
make help       # Show available targets
```

### Build Process
1. **Architecture Layer**: Bootloader, CPU management (GDT/IDT/ISR/IRQ), syscall interface
2. **Kernel Core**: Initialization, process/scheduler, timers, shell
3. **Memory Management**: Physical/virtual memory, kernel allocator
4. **Filesystem**: VFS interface, FAT12/FAT32 implementations
5. **Drivers**: Block, character, video, network, bus drivers
6. **Libraries**: libc (userspace), libk (kernel utilities)
7. **Linking**: All objects linked into single kernel binary
8. **ISO Creation**: Kernel packaged with GRUB bootloader

### Compiler Flags
- `-m32`: 32-bit x86 target
- `-ffreestanding`: Bare-metal, no standard library
- `-nostdlib -nodefaultlibs`: Disable all standard libraries
- `-fno-builtin`: Prevent GCC intrinsics
- `-I.`: Search current directory for headers
- `-Iarch/x86/include`: Architecture headers
- `-Iinclude`: Public headers
- `-Ilib/libc`: C library headers

## Design Principles

### Separation of Concerns
- **Architecture-specific code**: Isolated in `arch/x86/`, easy to port
- **Kernel core**: Platform-independent logic in `kernel/`
- **Device drivers**: Categorized by type (block/char/video/net/bus)
- **Filesystem**: VFS abstraction separates implementation from interface

### Modularity
Each directory represents a logical component with clear responsibilities:
- `arch/` → Hardware abstraction
- `kernel/` → Core OS functionality
- `mm/` → Memory management
- `fs/` → Filesystem operations
- `drivers/` → Hardware device support
- `lib/` → Reusable libraries

### Scalability
- Easy to add new architectures: Create `arch/<new_arch>/`
- Easy to add new filesystems: Add to `fs/<fs_name>/`
- Easy to add new drivers: Add to appropriate `drivers/<type>/`
- Easy to extend syscalls: Add to `kernel/syscall/`

## Include Path Strategy

### Source Files Include Paths
All source files use relative paths from project root:
```c
#include "arch/x86/sys.h"           // Architecture headers
#include "kernel/init/prg.h"        // Kernel headers
#include "mm/kmalloc.h"             // Memory management
#include "fs/fat32/fat32.h"         // Filesystem headers
#include "drivers/block/ata.h"      // Driver headers
#include "lib/libc/stdio.h"         // Library headers
```

### Compiler Include Paths
Makefile configures compiler with:
- `-I.` → Project root (all relative paths work)
- `-Iarch/x86/include` → Architecture-specific headers
- `-Iinclude` → Public API headers
- `-Ilib/libc` → C library headers

## Migration from Old Structure

### What Changed
1. **Flat `src/` removed**: All code now in logical hierarchy
2. **`src/boot/` → `arch/x86/boot/` and `arch/x86/cpu/`**: Boot and CPU management
3. **`src/kernel/` split**: Core functionality distributed:
   - Init code → `kernel/init/`
   - Syscalls → `kernel/syscall/`
   - Processes → `kernel/proc/`
   - Scheduler → `kernel/sched/`
   - Timers → `kernel/time/`
   - Commands → `kernel/shell/`
   - Memory → `mm/`
4. **`src/filesystem/` → `fs/`**: Renamed for brevity
5. **`src/drivers/` reorganized**: Grouped by driver type
6. **`src/toolchain/` → `lib/libc/`**: Clarifies purpose
7. **`src/cli/` → `userspace/bin/`**: Standard userspace location

### Backward Compatibility
- Original files preserved in `src/` (not removed)
- Old Makefile saved as `Makefile.old`
- Original README saved as `README-ORIGINAL.md`

## Development Workflow

### Adding a New Driver
1. Create `drivers/<type>/<driver_name>.{c,h}`
2. Implement driver interface
3. Copy header to `include/drivers/<type>/`
4. Makefile automatically detects and compiles

### Adding a New Syscall
1. Add function to appropriate kernel module
2. Add entry to `kernel/syscall/syscall_table.c`
3. Add case in `kernel/syscall/syscall.c` dispatcher
4. Define constant in `arch/x86/include/sys.h`

### Adding a Kernel Module
1. Create directory in `kernel/<module_name>/`
2. Implement functionality
3. Update Makefile if non-standard pattern
4. Add headers to `include/kernel/<module_name>/`

## Testing

### Run in QEMU
```bash
make run
```

### Debug with GDB
```bash
# Terminal 1
qemu-system-x86_64 -cdrom kernel.iso -s -S

# Terminal 2
gdb build/kernel.bin
(gdb) target remote :1234
(gdb) continue
```

## Future Enhancements

### Potential Additions
- `include/uapi/`: User-kernel API headers
- `kernel/ipc/`: Inter-process communication
- `kernel/vfs/`: Move VFS from fs/ to kernel/
- `arch/x86_64/`: 64-bit x86 support
- `lib/libk/`: Kernel-only utility functions
- `tools/`: Build tools and utilities

## References
- Linux kernel source tree structure
- BSD operating system layout
- Microkernel design patterns
- Original project README: `README-ORIGINAL.md`
