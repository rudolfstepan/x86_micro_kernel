# Quick Start Guide - Reorganized x86 Microkernel

## What Changed?

Your x86 microkernel has been reorganized from a flat structure into a professional, hierarchical operating system architecture. **No code functionality was changed** - only the organization.

## New Structure at a Glance

```
┌─ arch/x86/          → x86-specific (boot, CPU, paging)
├─ kernel/            → Core kernel (init, syscall, scheduler, process, shell)
├─ mm/                → Memory management (kmalloc)
├─ fs/                → Filesystems (VFS, FAT12, FAT32)
├─ drivers/           → Device drivers (block, char, video, net, bus)
├─ lib/libc/          → User C library
├─ userspace/bin/     → User programs
├─ config/            → Linker scripts, GRUB config
└─ scripts/           → Build scripts
```

## Building the Kernel

### Prerequisites (unchanged)
- Linux or WSL2
- GCC, NASM, GRUB, QEMU

### Build Commands
```bash
make clean      # Clean build artifacts
make all        # Build kernel + ISO
make run        # Build and run in QEMU
make help       # Show all targets
```

### What Happens During Build
1. Architecture code compiles → `build/arch/`
2. Kernel code compiles → `build/kernel/`
3. Memory, FS, drivers compile → `build/mm/`, `build/fs/`, `build/drivers/`
4. Libraries compile → `build/lib/`
5. Everything links into `build/kernel.bin`
6. ISO created with GRUB → `kernel.iso`

## Key Files and Their New Locations

### Finding Specific Code

| What you're looking for | New location |
|------------------------|--------------|
| **Kernel entry point** | `kernel/init/kernel.c` |
| **Boot sequence** | `arch/x86/boot/multiboot.asm` |
| **GDT/IDT setup** | `arch/x86/cpu/gdt.{asm,c}`, `arch/x86/cpu/idt.{asm,c}` |
| **Interrupt handlers** | `arch/x86/cpu/isr.{asm,c}`, `arch/x86/cpu/irq.{asm,c}` |
| **System calls** | `arch/x86/cpu/syscall.asm`, `kernel/init/kernel.c` |
| **Process management** | `kernel/proc/process.{c,h}` |
| **Scheduler** | `kernel/sched/scheduler.{c,h}` |
| **Memory allocator** | `mm/kmalloc.{c,h}` |
| **Paging** | `arch/x86/mm/paging.{c,h}` |
| **Shell/commands** | `kernel/shell/command.{c,h}` |
| **FAT32 filesystem** | `fs/fat32/fat32.{c,h}` |
| **ATA driver** | `drivers/block/ata.{c,h}` |
| **Keyboard driver** | `drivers/char/kb.{c,h}` |
| **VGA driver** | `drivers/video/video.{c,h}` |
| **Network drivers** | `drivers/net/{ne2000,e1000,rtl8139}.{c,h}` |
| **C library** | `lib/libc/{stdio,stdlib,string}.{c,h}` |
| **User programs** | `userspace/bin/{date,dir,test}.c` |

## Adding New Code

### New Device Driver
```bash
# 1. Create driver file
# For block device:
drivers/block/my_disk.c
drivers/block/my_disk.h

# For char device:
drivers/char/my_serial.c
drivers/char/my_serial.h

# 2. Write driver code
# 3. Makefile automatically detects and compiles it
make all
```

### New Kernel Module
```bash
# 1. Create module directory
mkdir kernel/mymodule

# 2. Add source files
kernel/mymodule/mymodule.c
kernel/mymodule/mymodule.h

# 3. Update Makefile to include it (add to KERNEL_* variables)
# 4. Build
make all
```

### New System Call
```c
// 1. Add function in appropriate module (e.g., kernel/proc/process.c)
void my_syscall(int arg) {
    // implementation
}

// 2. Add to syscall table in kernel/init/kernel.c
void* syscall_table[512] = {
    // ...
    (void*)&my_syscall,  // Syscall 9
};

// 3. Add constant in arch/x86/include/sys.h
#define SYS_MY_SYSCALL 9

// 4. Add handler case in syscall_handler()
case SYS_MY_SYSCALL:
    ((void (*)(int))func_ptr)(arg1);
    break;
```

### New Shell Command
```c
// 1. Add handler in kernel/shell/command.c
void cmd_mycommand(int cnt, const char **args) {
    printf("My command executed!\n");
}

// 2. Add to command table
command_t command_table[] = {
    // ...
    {"mycommand", cmd_mycommand},
};
```

## Include Paths in Your Code

When writing code, use these include patterns:

```c
// Architecture-specific
#include "arch/x86/sys.h"
#include "arch/x86/mbheader.h"

// Kernel modules
#include "kernel/init/prg.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/shell/command.h"

// Memory management
#include "mm/kmalloc.h"

// Filesystem
#include "fs/vfs/filesystem.h"
#include "fs/fat32/fat32.h"

// Drivers
#include "drivers/block/ata.h"
#include "drivers/char/kb.h"
#include "drivers/video/video.h"

// Libraries
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
```

## Troubleshooting

### Build Fails with "file not found"
**Problem**: Include path incorrect
**Solution**: Check include statement uses correct path from project root

### Linker Errors
**Problem**: Object file not being compiled
**Solution**: Check Makefile includes your source directory in `*_C` or `*_ASM` variables

### "Multiboot header not found" Error
**Problem**: Linker script issue
**Solution**: Ensure `config/klink.ld` is being used and multiboot section is first

## Documentation

- **README-NEW.md** - Complete architecture documentation
- **REORGANIZATION.md** - Details of what was changed
- **.github/copilot-instructions.md** - AI agent instructions
- **README.md** - Original README (preserved)

## Safety: Rollback if Needed

Your original code is preserved:
- Original source: `src/` directory (untouched)
- Original Makefile: `Makefile.old`
- Original scripts: In `scripts/` with originals in root

To rollback:
```bash
cp Makefile.old Makefile
# Build from src/ using old structure
```

## Testing Your Changes

```bash
# 1. Clean build
make clean

# 2. Build kernel
make all

# 3. Run in QEMU
make run

# 4. Test in QEMU shell
help          # List commands
ls /          # Test filesystem
mem           # Check memory
```

## Summary

✅ **Code unchanged** - Only organization improved  
✅ **Original preserved** - Can rollback anytime  
✅ **Build system updated** - Works with new structure  
✅ **Fully documented** - READMEs and comments  
✅ **Professional structure** - Follows OS standards  

## Questions?

Check these files:
- `README-NEW.md` - Architecture details
- `REORGANIZATION.md` - What changed
- `Makefile` - Build system (`make help`)
- `.github/copilot-instructions.md` - Development guide

## Next Steps

1. **Test the build**: `make clean && make all`
2. **Run in QEMU**: `make run`
3. **Review structure**: Explore new directories
4. **Continue development**: Use new organized structure
5. **Contribute**: Structure makes collaboration easier!

---

*Reorganized on October 31, 2025*
*All original files preserved for safety*
