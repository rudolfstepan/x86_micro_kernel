# REIST OS Makefile - Reorganized Structure
# Build system for hierarchical OS architecture

# ============================================================================
# CONFIGURATION
# ============================================================================

# Architecture and platform
ARCH := x86
PLATFORM := pc

# Directories
ARCH_DIR := arch/$(ARCH)
KERNEL_DIR := kernel
MM_DIR := mm
FS_DIR := fs
DRIVERS_DIR := drivers
LIB_DIR := lib
USERSPACE_DIR := userspace
CONFIG_DIR := config
OUTPUT_DIR := build

# Output subdirectories
BUILD_ARCH_DIR := $(OUTPUT_DIR)/arch
BUILD_KERNEL_DIR := $(OUTPUT_DIR)/kernel
BUILD_MM_DIR := $(OUTPUT_DIR)/mm
BUILD_FS_DIR := $(OUTPUT_DIR)/fs
BUILD_DRIVERS_DIR := $(OUTPUT_DIR)/drivers
BUILD_LIB_DIR := $(OUTPUT_DIR)/lib
BUILD_USERSPACE_DIR := $(OUTPUT_DIR)/userspace

# Toolchain
AS := nasm
CC := gcc
LD := ld
OBJCOPY := objcopy
ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
ZIG ?= zig
USER_PROGRAM_SOURCE ?= examples/userspace/hello.c
USER_PROGRAM_OUTPUT ?= $(OUTPUT_DIR)/programs/HELLO.PRG
SYSTEM_PROGRAM_DIR := $(OUTPUT_DIR)/programs

# Build target selection (default: qemu)
# Override with: make TARGET=real_hw or TARGET=vmware
TARGET ?= qemu

# Video mode selection (default: vga)
# Override with: make VIDEO=framebuffer
VIDEO ?= vga
FAULT_INJECTION ?= 0
MEMORY_FAULT_INJECTION ?= 0
STORAGE_FAULT_INJECTION ?= 0
STORAGE_IO_FAULT_INJECTION ?= 0
HANDOVER_FAULT_INJECTION ?= 0
DHCP_LEASE_FAULT_INJECTION ?= 0
HANDOVER_NODE_ID ?= 0

# Target-specific defines
ifeq ($(TARGET),real_hw)
    TARGET_DEFINES := -DREAL_HARDWARE -DATA_STRICT_TIMING -DFAT32_STRICT_VALIDATION
else ifeq ($(TARGET),qemu)
    TARGET_DEFINES := -DQEMU_BUILD -DATA_RELAXED_TIMING
else ifeq ($(TARGET),vmware)
    TARGET_DEFINES := -DVMWARE_BUILD -DATA_MODERATE_TIMING
else
    $(error Invalid TARGET=$(TARGET). Use 'qemu', 'vmware', or 'real_hw')
endif

ifeq ($(FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES := -DREIST_FAULT_INJECTION
else
    SAFETY_TEST_DEFINES :=
endif

ifeq ($(MEMORY_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_MEMORY_FAULT_INJECTION
endif

ifeq ($(STORAGE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_STORAGE_FAULT_INJECTION
endif

ifeq ($(STORAGE_IO_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_STORAGE_IO_FAULT_INJECTION
endif

ifeq ($(HANDOVER_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_HANDOVER_FAULT_INJECTION \
        -DREIST_HANDOVER_NODE_ID=$(HANDOVER_NODE_ID)
endif

ifeq ($(DHCP_LEASE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_DHCP_LEASE_TEST_MS=2500U
endif

ifeq ($(DHCP_RENEW_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_DHCP_RENEW_TEST_MS=5000U
endif

# Video mode defines
ifeq ($(VIDEO),framebuffer)
    VIDEO_DEFINES := -DUSE_FRAMEBUFFER
else ifeq ($(VIDEO),vga)
    VIDEO_DEFINES := -DUSE_VGA_TEXT
else
    $(error Invalid VIDEO=$(VIDEO). Use 'vga' or 'framebuffer')
endif

# Compiler flags
ASFLAGS := -f elf32
ifeq ($(VIDEO),framebuffer)
    ASFLAGS += -DUSE_FRAMEBUFFER
endif

FRAME_WARNING_FLAGS ?= -Wframe-larger-than=4096 -Werror=frame-larger-than
CFLAGS := -m32 -std=gnu11 -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
          -fno-builtin -fno-pic -fno-pie -fno-stack-protector \
          -Werror=vla $(FRAME_WARNING_FLAGS) \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx \
          -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -U_FORTIFY_SOURCE \
          -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
          -Werror=int-conversion -Werror=return-type \
          -I$(OUTPUT_DIR) -I. -I$(ARCH_DIR) -I$(ARCH_DIR)/include -I$(LIB_DIR)/libc -I$(KERNEL_DIR)/shell \
          $(TARGET_DEFINES) $(VIDEO_DEFINES) $(SAFETY_TEST_DEFINES) \
          $(STACK_ANALYSIS_FLAGS)
DEPFLAGS = -MMD -MP -MF $(@:.o=.d) -MT $@
STACK_ANALYSIS_CC ?= gcc
STACK_ANALYSIS_OUTPUT_DIR ?= build/stack-analysis

LDFLAGS := -m elf_i386 -nostdlib --strip-all --build-id=sha1
KERNEL_LDSCRIPT := $(CONFIG_DIR)/klink.ld

# Mount directory for disk image
MOUNT_DIR := /mnt/disk

# ============================================================================
# SOURCE FILES
# ============================================================================

# Architecture sources (x86)
ARCH_BOOT_ASM := $(wildcard $(ARCH_DIR)/boot/*.asm)
ARCH_BOOT_C := $(wildcard $(ARCH_DIR)/boot/*.c)
ARCH_CPU_ASM := $(wildcard $(ARCH_DIR)/cpu/*.asm)
ARCH_CPU_C := $(wildcard $(ARCH_DIR)/cpu/*.c)
ARCH_MM_C := $(wildcard $(ARCH_DIR)/mm/*.c)

# Kernel sources
KERNEL_INIT_C := $(wildcard $(KERNEL_DIR)/init/*.c)
KERNEL_SYSCALL_C := $(wildcard $(KERNEL_DIR)/syscall/*.c)
KERNEL_PROC_C := $(wildcard $(KERNEL_DIR)/proc/*.c)
KERNEL_IPC_C := $(wildcard $(KERNEL_DIR)/ipc/*.c)
KERNEL_SCHED_C := $(wildcard $(KERNEL_DIR)/sched/*.c)
KERNEL_SCHED_ASM := $(wildcard $(KERNEL_DIR)/sched/*.asm)
KERNEL_TIME_C := $(wildcard $(KERNEL_DIR)/time/*.c)
KERNEL_SHELL_C := $(wildcard $(KERNEL_DIR)/shell/*.c)

# Memory management sources
MM_C := $(wildcard $(MM_DIR)/*.c)

# Filesystem sources
FS_VFS_C := $(wildcard $(FS_DIR)/vfs/*.c)
FS_FAT12_C := $(wildcard $(FS_DIR)/fat12/*.c)
FS_FAT32_C := $(wildcard $(FS_DIR)/fat32/*.c)
FS_EXT2_C := $(wildcard $(FS_DIR)/ext2/*.c)

# Driver sources
DRIVERS_BLOCK_C := $(wildcard $(DRIVERS_DIR)/block/*.c)
DRIVERS_CHAR_C := $(wildcard $(DRIVERS_DIR)/char/*.c)
DRIVERS_VIDEO_C := $(wildcard $(DRIVERS_DIR)/video/*.c)
DRIVERS_NET_C := $(wildcard $(DRIVERS_DIR)/net/*.c)
DRIVERS_BUS_C := $(wildcard $(DRIVERS_DIR)/bus/*.c)
DRIVERS_USB_C := $(wildcard $(DRIVERS_DIR)/usb/*.c)

# Library sources
LIB_LIBC_C := $(wildcard $(LIB_DIR)/libc/*.c)
LIB_LIBC_ASM := $(wildcard $(LIB_DIR)/libc/*.asm)
LIB_LIBK_C := $(wildcard $(LIB_DIR)/libk/*.c)

CONFIG_STAMP := $(OUTPUT_DIR)/.config-$(TARGET)-$(VIDEO)

# ============================================================================
# OBJECT FILES
# ============================================================================

# Architecture objects
ARCH_BOOT_ASM_OBJ := $(patsubst $(ARCH_DIR)/boot/%.asm,$(BUILD_ARCH_DIR)/boot/%.o,$(ARCH_BOOT_ASM))
ARCH_BOOT_C_OBJ := $(patsubst $(ARCH_DIR)/boot/%.c,$(BUILD_ARCH_DIR)/boot/%.o,$(ARCH_BOOT_C))
ARCH_CPU_ASM_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.asm,$(BUILD_ARCH_DIR)/cpu/%_asm.o,$(ARCH_CPU_ASM))
ARCH_CPU_C_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.c,$(BUILD_ARCH_DIR)/cpu/%.o,$(ARCH_CPU_C))
ARCH_MM_OBJ := $(patsubst $(ARCH_DIR)/mm/%.c,$(BUILD_ARCH_DIR)/mm/%.o,$(ARCH_MM_C))

# Kernel objects
KERNEL_INIT_OBJ := $(patsubst $(KERNEL_DIR)/init/%.c,$(BUILD_KERNEL_DIR)/init/%.o,$(KERNEL_INIT_C))
KERNEL_SYSCALL_OBJ := $(patsubst $(KERNEL_DIR)/syscall/%.c,$(BUILD_KERNEL_DIR)/syscall/%.o,$(KERNEL_SYSCALL_C))
KERNEL_PROC_OBJ := $(patsubst $(KERNEL_DIR)/proc/%.c,$(BUILD_KERNEL_DIR)/proc/%.o,$(KERNEL_PROC_C))
KERNEL_IPC_OBJ := $(patsubst $(KERNEL_DIR)/ipc/%.c,$(BUILD_KERNEL_DIR)/ipc/%.o,$(KERNEL_IPC_C))
KERNEL_SCHED_C_OBJ := $(patsubst $(KERNEL_DIR)/sched/%.c,$(BUILD_KERNEL_DIR)/sched/%.o,$(KERNEL_SCHED_C))
KERNEL_SCHED_ASM_OBJ := $(patsubst $(KERNEL_DIR)/sched/%.asm,$(BUILD_KERNEL_DIR)/sched/%.o,$(KERNEL_SCHED_ASM))
KERNEL_TIME_OBJ := $(patsubst $(KERNEL_DIR)/time/%.c,$(BUILD_KERNEL_DIR)/time/%.o,$(KERNEL_TIME_C))
KERNEL_SHELL_OBJ := $(patsubst $(KERNEL_DIR)/shell/%.c,$(BUILD_KERNEL_DIR)/shell/%.o,$(KERNEL_SHELL_C))

# Memory management objects
MM_OBJ := $(patsubst $(MM_DIR)/%.c,$(BUILD_MM_DIR)/%.o,$(MM_C))

# Filesystem objects
FS_VFS_OBJ := $(patsubst $(FS_DIR)/vfs/%.c,$(BUILD_FS_DIR)/vfs/%.o,$(FS_VFS_C))
FS_FAT12_OBJ := $(patsubst $(FS_DIR)/fat12/%.c,$(BUILD_FS_DIR)/fat12/%.o,$(FS_FAT12_C))
FS_FAT32_OBJ := $(patsubst $(FS_DIR)/fat32/%.c,$(BUILD_FS_DIR)/fat32/%.o,$(FS_FAT32_C))
FS_EXT2_OBJ := $(patsubst $(FS_DIR)/ext2/%.c,$(BUILD_FS_DIR)/ext2/%.o,$(FS_EXT2_C))

# Driver objects
DRIVERS_BLOCK_OBJ := $(patsubst $(DRIVERS_DIR)/block/%.c,$(BUILD_DRIVERS_DIR)/block/%.o,$(DRIVERS_BLOCK_C))
DRIVERS_CHAR_OBJ := $(patsubst $(DRIVERS_DIR)/char/%.c,$(BUILD_DRIVERS_DIR)/char/%.o,$(DRIVERS_CHAR_C))
DRIVERS_VIDEO_OBJ := $(patsubst $(DRIVERS_DIR)/video/%.c,$(BUILD_DRIVERS_DIR)/video/%.o,$(DRIVERS_VIDEO_C))
DRIVERS_NET_OBJ := $(patsubst $(DRIVERS_DIR)/net/%.c,$(BUILD_DRIVERS_DIR)/net/%.o,$(DRIVERS_NET_C))
DRIVERS_BUS_OBJ := $(patsubst $(DRIVERS_DIR)/bus/%.c,$(BUILD_DRIVERS_DIR)/bus/%.o,$(DRIVERS_BUS_C))
DRIVERS_USB_OBJ := $(patsubst $(DRIVERS_DIR)/usb/%.c,$(BUILD_DRIVERS_DIR)/usb/%.o,$(DRIVERS_USB_C))

# Library objects
LIB_LIBC_C_OBJ := $(patsubst $(LIB_DIR)/libc/%.c,$(BUILD_LIB_DIR)/libc/%.o,$(LIB_LIBC_C))
LIB_LIBC_ASM_OBJ := $(patsubst $(LIB_DIR)/libc/%.asm,$(BUILD_LIB_DIR)/libc/%.o,$(LIB_LIBC_ASM))
LIB_LIBK_OBJ := $(patsubst $(LIB_DIR)/libk/%.c,$(BUILD_LIB_DIR)/libk/%.o,$(LIB_LIBK_C))

# Aggregate objects for linking
ARCH_OBJ := $(ARCH_BOOT_ASM_OBJ) $(ARCH_BOOT_C_OBJ) $(ARCH_CPU_ASM_OBJ) $(ARCH_CPU_C_OBJ) $(ARCH_MM_OBJ)
KERNEL_OBJ := $(KERNEL_INIT_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_PROC_OBJ) \
              $(KERNEL_IPC_OBJ) \
              $(KERNEL_SCHED_C_OBJ) $(KERNEL_SCHED_ASM_OBJ) $(KERNEL_TIME_OBJ) \
              $(KERNEL_SHELL_OBJ)
FS_OBJ := $(FS_VFS_OBJ) $(FS_FAT12_OBJ) $(FS_FAT32_OBJ) $(FS_EXT2_OBJ)
DRIVERS_OBJ := $(DRIVERS_BLOCK_OBJ) $(DRIVERS_CHAR_OBJ) $(DRIVERS_VIDEO_OBJ) \
               $(DRIVERS_NET_OBJ) $(DRIVERS_USB_OBJ) $(DRIVERS_BUS_OBJ)
LIB_OBJ := $(LIB_LIBC_C_OBJ) $(LIB_LIBC_ASM_OBJ) $(LIB_LIBK_OBJ)

ALL_OBJ := $(ARCH_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(FS_OBJ) $(DRIVERS_OBJ) $(LIB_OBJ)
C_OBJ := $(ARCH_BOOT_C_OBJ) $(ARCH_CPU_C_OBJ) $(ARCH_MM_OBJ) \
         $(KERNEL_INIT_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_PROC_OBJ) \
         $(KERNEL_IPC_OBJ) $(KERNEL_SCHED_C_OBJ) $(KERNEL_TIME_OBJ) \
         $(KERNEL_SHELL_OBJ) $(MM_OBJ) $(FS_OBJ) $(DRIVERS_OBJ) \
         $(LIB_LIBC_C_OBJ) $(LIB_LIBK_OBJ)
DEPS := $(C_OBJ:.o=.d)

# Ensure the generated directory tree exists before any object is built.
# This also makes direct and parallel `make kernel` invocations reliable.
$(ALL_OBJ): $(CONFIG_STAMP) | prepare

# Object paths are shared across configurations.  A target/video switch must
# therefore invalidate all objects even when no source timestamp changed.
$(CONFIG_STAMP):
	@$(PYTHON) -c "from pathlib import Path; p=Path('$(OUTPUT_DIR)'); p.mkdir(parents=True, exist_ok=True); [f.unlink() for f in p.glob('.config-*')]; Path('$@').touch()"

-include $(DEPS)

# ============================================================================
# TARGETS
# ============================================================================

.PHONY: all clean prepare kernel check-kernel-dependencies check-kernel-stack check-kernel-stack-analysis user-program system-programs bootdisk native-image floppy-image run run-disk run-native run-floppy run-fb help format-disks test test-unit test-all test-images test-smoke test-smoke-pit test-smoke-watchdog test-smoke-fatal-recovery test-smoke-memory-fault test-smoke-journal-recovery test-smoke-storage-recovery test-smoke-storage-io-failure test-smoke-handover test-smoke-handover-pair test-smoke-memory test-smoke-desktop test-verbose test-bash test-quick run-debug print-vars build-qemu build-qemu-fb build-vmware build-real-hw clean-all

all: native-image

# Build specifically for QEMU (relaxed timing)
build-qemu:
	@echo "Building kernel for QEMU emulation..."
	@$(MAKE) clean
	@$(MAKE) all TARGET=qemu
	@echo "✓ QEMU build complete: $(OUTPUT_DIR)/reist-os.img"
	@echo "  Run with: make run"

# Build for QEMU with framebuffer
build-qemu-fb:
	@echo "Building kernel for QEMU with framebuffer..."
	@$(MAKE) clean
	@$(MAKE) all TARGET=qemu VIDEO=framebuffer
	@echo "✓ QEMU framebuffer build complete: $(OUTPUT_DIR)/reist-os.img"
	@echo "  Run with: make run-fb"

# Build specifically for VMware Workstation
build-vmware:
	@echo "Building kernel for VMware Workstation..."
	@echo "  - Intel E1000 network adapter enabled"
	@echo "  - VMware-optimized timing configuration"
	@$(MAKE) clean
	@$(MAKE) all TARGET=vmware
	@echo "✓ VMware build complete: $(OUTPUT_DIR)/vmware/reist-os/reist-os.vmx"
	@echo ""
	@echo "VMware Configuration Instructions:"
	@echo "  1. Create New VM → Custom (advanced)"
	@echo "  2. Guest OS: Other → Other (32-bit)"
	@echo "  3. Memory: 64 MB"
	@echo "  4. Network Adapter → Advanced → Intel E1000"
	@echo "  5. Open $(OUTPUT_DIR)/vmware/reist-os/reist-os.vmx"
	@echo "  6. Boot and enjoy!"

# Build specifically for real hardware (strict timing)
build-real-hw:
	@echo "Building kernel for real hardware..."
	@$(MAKE) clean
	@$(MAKE) all TARGET=real_hw
	@echo "✓ Real hardware build complete: $(OUTPUT_DIR)/reist-os.img"
	@echo "  Write to USB: dd if=$(OUTPUT_DIR)/reist-os.img of=/dev/sdX bs=4M"

format-disks:
	@echo "Formatting disk images..."
	@./scripts/format_disks.sh

help:
	@echo "REIST OS Build System"
	@echo "============================"
	@echo ""
	@echo "Build Targets:"
	@echo "  all          - Build the native BIOS disk image (default, TARGET=$(TARGET))"
	@echo "  build-qemu   - Build specifically for QEMU (relaxed ATA timing)"
	@echo "  build-qemu-fb - Build for QEMU with framebuffer support"
	@echo "  build-vmware - Build for VMware Workstation (E1000 network)"
	@echo "  build-real-hw - Build for real hardware (strict ATA timing)"
	@echo "  kernel       - Build kernel binary only"
	@echo "  user-program - Compile USER_PROGRAM_SOURCE into a loadable MYPR file"
	@echo "  system-programs - Build the native Ring-3 system tool collection"
	@echo "  bootdisk     - Create native BIOS disk image"
	@echo "  floppy-image - Create a 1.44-MB BIOS floppy image"
	@echo "  clean        - Remove all build artifacts"
	@echo ""
	@echo "Run Targets:"
	@echo "  run          - Build and run the native image in QEMU"
	@echo "  run-disk     - Build and run in QEMU (from bootable disk)"
	@echo "  run-debug    - Build and run in QEMU with GDB debugging"
	@echo ""
	@echo "Network Adapter Targets:"
	@echo "  run-rtl8139      - Run with RTL8139 (Realtek, best QEMU support)"
	@echo "  run-rtl8139-tap  - Run RTL8139 with TAP networking"
	@echo "  run-e1000        - Run with E1000 (Intel Gigabit)"
	@echo "  run-e1000-tap    - Run E1000 with TAP networking"
	@echo "  run-ne2000       - Run with NE2000 (legacy)"
	@echo "  run-ne2000-tap   - Run NE2000 with TAP networking"
	@echo ""
	@echo "Test Targets:"
	@echo "  test         - Run fixture-independent host regression tests"
	@echo "  test-all     - Run unit tests plus generated-image integration tests"
	@echo "  test-images  - Validate generated disk images"
	@echo "  test-smoke   - Boot QEMU and run the automated Ring-3 guest test"
	@echo "  test-smoke-pit - Run the guest test with the LAPIC disabled"
	@echo "  test-smoke-memory - Run guest tests with 32/64/256/1024 MiB RAM"
	@echo "  test-smoke-desktop - Boot the framebuffer desktop and capture it"
	@echo "  test-verbose - Validate disk images with detailed output"
	@echo "  test-bash    - Run disk image tests (Bash, no Python required)"
	@echo "  test-quick   - Quick check if disk images exist"
	@echo ""
	@echo "Utility Targets:"
	@echo "  format-disks - Format disk.img and floppy.img with FAT filesystems"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Build Configuration:"
	@echo "  Current TARGET: $(TARGET)"
	@echo "  QEMU build    : Relaxed ATA timing, optimized for emulation"
	@echo "  Real HW build : Strict ATA timing, proper IRQ handling"
	@echo "  Override with : make TARGET=real_hw all"
	@echo ""
	@echo "Directory Structure:"
	@echo "  arch/     - Architecture-specific code (x86)"
	@echo "  kernel/   - Core kernel (init, syscall, sched, proc, time, shell)"
	@echo "  mm/       - Memory management"
	@echo "  fs/       - Filesystem (VFS, FAT12, FAT32)"
	@echo "  drivers/  - Device drivers (block, char, video, net, bus)"
	@echo "  lib/      - Libraries (libc, libk)"
	@echo ""
	@echo "Disk Images:"
	@echo "  disk.img   - Primary Master (hdd0) - FAT32"
	@echo "  disk1.img  - Primary Slave (hdd1) - FAT32"
	@echo "  floppy.img - Floppy drive (fd0) - FAT12"

clean:
	@echo "Cleaning build artifacts..."
	@$(PYTHON) -c "import shutil; from pathlib import Path; p=Path('$(OUTPUT_DIR)'); shutil.rmtree(p, ignore_errors=True); p.mkdir(parents=True, exist_ok=True)"

prepare:
	@echo "Creating build directories..."
	@$(PYTHON) -c "from pathlib import Path; [Path(p).mkdir(parents=True, exist_ok=True) for p in '$(BUILD_ARCH_DIR)/boot $(BUILD_ARCH_DIR)/cpu $(BUILD_ARCH_DIR)/mm $(BUILD_KERNEL_DIR)/init $(BUILD_KERNEL_DIR)/syscall $(BUILD_KERNEL_DIR)/proc $(BUILD_KERNEL_DIR)/ipc $(BUILD_KERNEL_DIR)/sched $(BUILD_KERNEL_DIR)/time $(BUILD_KERNEL_DIR)/shell $(BUILD_MM_DIR) $(BUILD_FS_DIR)/vfs $(BUILD_FS_DIR)/fat12 $(BUILD_FS_DIR)/fat32 $(BUILD_FS_DIR)/ext2 $(BUILD_DRIVERS_DIR)/block $(BUILD_DRIVERS_DIR)/char $(BUILD_DRIVERS_DIR)/video $(BUILD_DRIVERS_DIR)/net $(BUILD_DRIVERS_DIR)/bus $(BUILD_DRIVERS_DIR)/usb $(BUILD_LIB_DIR)/libc $(BUILD_LIB_DIR)/libk $(BUILD_USERSPACE_DIR)/bin'.split()]"

# ============================================================================
# COMPILATION RULES
# ============================================================================

# Architecture - Boot assembly
$(BUILD_ARCH_DIR)/boot/%.o: $(ARCH_DIR)/boot/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Architecture - Boot C
$(BUILD_ARCH_DIR)/boot/%.o: $(ARCH_DIR)/boot/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Architecture - CPU assembly (renamed to avoid conflicts with C files)
$(BUILD_ARCH_DIR)/cpu/%_asm.o: $(ARCH_DIR)/cpu/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Architecture - CPU C
$(BUILD_ARCH_DIR)/cpu/%.o: $(ARCH_DIR)/cpu/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Architecture - Memory management
$(BUILD_ARCH_DIR)/mm/%.o: $(ARCH_DIR)/mm/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Init
$(BUILD_KERNEL_DIR)/init/%.o: $(KERNEL_DIR)/init/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Syscall
$(BUILD_KERNEL_DIR)/syscall/%.o: $(KERNEL_DIR)/syscall/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Process
$(BUILD_KERNEL_DIR)/proc/%.o: $(KERNEL_DIR)/proc/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Scheduler C
$(BUILD_KERNEL_DIR)/sched/%.o: $(KERNEL_DIR)/sched/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Scheduler ASM
$(BUILD_KERNEL_DIR)/sched/%.o: $(KERNEL_DIR)/sched/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Kernel - Time
$(BUILD_KERNEL_DIR)/time/%.o: $(KERNEL_DIR)/time/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - Shell
$(BUILD_KERNEL_DIR)/shell/%.o: $(KERNEL_DIR)/shell/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Kernel - IPC
$(BUILD_KERNEL_DIR)/ipc/%.o: $(KERNEL_DIR)/ipc/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Memory management
$(BUILD_MM_DIR)/%.o: $(MM_DIR)/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Filesystem - VFS
$(BUILD_FS_DIR)/vfs/%.o: $(FS_DIR)/vfs/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Filesystem - FAT12
$(BUILD_FS_DIR)/fat12/%.o: $(FS_DIR)/fat12/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Filesystem - FAT32
$(BUILD_FS_DIR)/fat32/%.o: $(FS_DIR)/fat32/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Filesystem - EXT2
$(BUILD_FS_DIR)/ext2/%.o: $(FS_DIR)/ext2/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - Block
$(BUILD_DRIVERS_DIR)/block/%.o: $(DRIVERS_DIR)/block/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - Char
$(BUILD_DRIVERS_DIR)/char/%.o: $(DRIVERS_DIR)/char/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - Video
$(BUILD_DRIVERS_DIR)/video/%.o: $(DRIVERS_DIR)/video/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - Net
$(BUILD_DRIVERS_DIR)/net/%.o: $(DRIVERS_DIR)/net/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - Bus
$(BUILD_DRIVERS_DIR)/bus/%.o: $(DRIVERS_DIR)/bus/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Drivers - USB
$(BUILD_DRIVERS_DIR)/usb/%.o: $(DRIVERS_DIR)/usb/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Library - libc C
$(BUILD_LIB_DIR)/libc/%.o: $(LIB_DIR)/libc/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# Library - libc ASM
$(BUILD_LIB_DIR)/libc/%.o: $(LIB_DIR)/libc/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Library - libk
$(BUILD_LIB_DIR)/libk/%.o: $(LIB_DIR)/libk/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@

# ============================================================================
# KERNEL LINKING
# ============================================================================

check-kernel-dependencies: $(ALL_OBJ)
	@$(PYTHON) scripts/validate_build_dependencies.py $(DEPS)

kernel: check-kernel-dependencies
	@echo "Linking kernel..."
	@$(LD) $(LDFLAGS) -T $(KERNEL_LDSCRIPT) -o $(OUTPUT_DIR)/kernel.bin $(ALL_OBJ)
	@echo "Release kernel ELF: $(OUTPUT_DIR)/kernel.bin"

check-kernel-stack: check-kernel-dependencies
	@echo "Kernel stack-frame and VLA compiler gates passed."

check-kernel-stack-analysis:
	@$(MAKE) clean OUTPUT_DIR=$(STACK_ANALYSIS_OUTPUT_DIR)
	@$(MAKE) check-kernel-stack OUTPUT_DIR=$(STACK_ANALYSIS_OUTPUT_DIR) \
		CC=$(STACK_ANALYSIS_CC) \
		FRAME_WARNING_FLAGS="-Wframe-larger-than=4096 -Werror=frame-larger-than=4096" \
		STACK_ANALYSIS_FLAGS="-fstack-usage -fcallgraph-info=su"
	@$(PYTHON) scripts/validate_stack_usage.py \
		--root $(STACK_ANALYSIS_OUTPUT_DIR) --expected 75 \
		--local-limit 4096

# ============================================================================
# BOOTABLE DISK IMAGE
# ============================================================================

bootdisk: native-image

user-program:
	@echo "Building external user program $(USER_PROGRAM_SOURCE)..."
	@$(PYTHON) -c "from pathlib import Path; Path('$(USER_PROGRAM_OUTPUT)').parent.mkdir(parents=True, exist_ok=True)"
	@$(PYTHON) scripts/build_user_program.py $(USER_PROGRAM_SOURCE) \
		--output $(USER_PROGRAM_OUTPUT) --zig $(ZIG)

system-programs:
	@echo "Building standard Ring-3 system programs..."
	@$(PYTHON) scripts/build_system_programs.py \
		--output-dir $(SYSTEM_PROGRAM_DIR) --zig $(ZIG)

native-image: floppy-image
	@echo "Creating native BIOS disk image..."
	@$(AS) -f bin arch/$(ARCH)/boot/bios/stage1_mbr.asm -o $(OUTPUT_DIR)/stage1_mbr.bin
	@$(AS) $(VIDEO_DEFINES) -f bin arch/$(ARCH)/boot/bios/stage2_bios.asm -o $(OUTPUT_DIR)/stage2_bios.bin
	@$(PYTHON) scripts/create_native_boot_image.py \
		--stage1 $(OUTPUT_DIR)/stage1_mbr.bin \
		--stage2 $(OUTPUT_DIR)/stage2_bios.bin \
		--kernel $(OUTPUT_DIR)/kernel.bin \
		--output $(OUTPUT_DIR)/reist-os.img \
		--vmdk $(OUTPUT_DIR)/reist-os.vmdk \
		--vmware-dir $(OUTPUT_DIR)/vmware/reist-os \
		--floppy $(OUTPUT_DIR)/reist-os-floppy.img \
		--data-file HELLO.PRG=$(USER_PROGRAM_OUTPUT) \
		--data-file SYSINFO.PRG=$(SYSTEM_PROGRAM_DIR)/SYSINFO.PRG \
		--data-file REPEAT.PRG=$(SYSTEM_PROGRAM_DIR)/REPEAT.PRG \
		--data-file CALC.PRG=$(SYSTEM_PROGRAM_DIR)/CALC.PRG \
		--data-file DATE.PRG=$(SYSTEM_PROGRAM_DIR)/DATE.PRG \
		--data-file UPTIME.PRG=$(SYSTEM_PROGRAM_DIR)/UPTIME.PRG \
		--data-file MEMINFO.PRG=$(SYSTEM_PROGRAM_DIR)/MEMINFO.PRG \
		--data-file ASCII.PRG=$(SYSTEM_PROGRAM_DIR)/ASCII.PRG \
		--data-file CAT.PRG=$(SYSTEM_PROGRAM_DIR)/CAT.PRG \
		--data-file LS.PRG=$(SYSTEM_PROGRAM_DIR)/LS.PRG \
		--data-file SAVE.PRG=$(SYSTEM_PROGRAM_DIR)/SAVE.PRG \
		--data-file BASIC.PRG=$(SYSTEM_PROGRAM_DIR)/BASIC.PRG \
		--data-file SPAWN.PRG=$(SYSTEM_PROGRAM_DIR)/SPAWN.PRG \
		--data-file PS.PRG=$(SYSTEM_PROGRAM_DIR)/PS.PRG \
		--data-file KILL.PRG=$(SYSTEM_PROGRAM_DIR)/KILL.PRG \
		--data-file PWD.PRG=$(SYSTEM_PROGRAM_DIR)/PWD.PRG \
		--data-file SHELL.PRG=$(SYSTEM_PROGRAM_DIR)/SHELL.PRG \
		--data-file DESKTOP.PRG=$(SYSTEM_PROGRAM_DIR)/DESKTOP.PRG \
		--data-file MKDIR.PRG=$(SYSTEM_PROGRAM_DIR)/MKDIR.PRG \
		--data-file RMDIR.PRG=$(SYSTEM_PROGRAM_DIR)/RMDIR.PRG \
		--data-file DEL.PRG=$(SYSTEM_PROGRAM_DIR)/DEL.PRG \
		--data-file COPY.PRG=$(SYSTEM_PROGRAM_DIR)/COPY.PRG \
		--data-file ECHO.PRG=$(SYSTEM_PROGRAM_DIR)/ECHO.PRG \
		--data-file CLS.PRG=$(SYSTEM_PROGRAM_DIR)/CLS.PRG \
		--data-file DRIVES.PRG=$(SYSTEM_PROGRAM_DIR)/DRIVES.PRG \
		--data-file EDIT.PRG=$(SYSTEM_PROGRAM_DIR)/EDIT.PRG \
		--data-file CHILDEX.PRG=$(SYSTEM_PROGRAM_DIR)/CHILDEX.PRG \
		--data-file FAULTDE.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTDE.PRG \
		--data-file FAULTUD.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTUD.PRG \
		--data-file FAULTPF.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTPF.PRG \
		--data-file FAULTSTK.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTSTK.PRG \
		--data-file GTEST.PRG=$(SYSTEM_PROGRAM_DIR)/GTEST.PRG \
		--data-file REIST.PRG=$(SYSTEM_PROGRAM_DIR)/REIST.PRG \
		--data-file STORAGE.PRG=$(SYSTEM_PROGRAM_DIR)/STORAGE.PRG \
		--data-file SLEEPER.PRG=$(SYSTEM_PROGRAM_DIR)/SLEEPER.PRG
	@echo "Native BIOS image created: $(OUTPUT_DIR)/reist-os.img"
	@echo "Complete VMware VM: $(OUTPUT_DIR)/vmware/reist-os/reist-os.vmx"

floppy-image: kernel system-programs user-program
	@echo "Creating 1.44-MB BIOS floppy image..."
	@$(AS) -f bin arch/$(ARCH)/boot/bios/stage1_floppy.asm -o $(OUTPUT_DIR)/stage1_floppy.bin
	@$(AS) $(VIDEO_DEFINES) -f bin arch/$(ARCH)/boot/bios/stage2_bios.asm -o $(OUTPUT_DIR)/stage2_bios.bin
	@$(PYTHON) scripts/create_floppy_boot_image.py \
		--stage1 $(OUTPUT_DIR)/stage1_floppy.bin \
		--stage2 $(OUTPUT_DIR)/stage2_bios.bin \
		--kernel $(OUTPUT_DIR)/kernel.bin \
		--output $(OUTPUT_DIR)/reist-os-floppy.img \
		--data-file HELLO.PRG=$(USER_PROGRAM_OUTPUT) \
		--data-file SYSINFO.PRG=$(SYSTEM_PROGRAM_DIR)/SYSINFO.PRG \
		--data-file REPEAT.PRG=$(SYSTEM_PROGRAM_DIR)/REPEAT.PRG \
		--data-file CALC.PRG=$(SYSTEM_PROGRAM_DIR)/CALC.PRG \
		--data-file DATE.PRG=$(SYSTEM_PROGRAM_DIR)/DATE.PRG \
		--data-file UPTIME.PRG=$(SYSTEM_PROGRAM_DIR)/UPTIME.PRG \
		--data-file MEMINFO.PRG=$(SYSTEM_PROGRAM_DIR)/MEMINFO.PRG \
		--data-file ASCII.PRG=$(SYSTEM_PROGRAM_DIR)/ASCII.PRG \
		--data-file CAT.PRG=$(SYSTEM_PROGRAM_DIR)/CAT.PRG \
		--data-file LS.PRG=$(SYSTEM_PROGRAM_DIR)/LS.PRG \
		--data-file SAVE.PRG=$(SYSTEM_PROGRAM_DIR)/SAVE.PRG \
		--data-file BASIC.PRG=$(SYSTEM_PROGRAM_DIR)/BASIC.PRG \
		--data-file SPAWN.PRG=$(SYSTEM_PROGRAM_DIR)/SPAWN.PRG \
		--data-file PS.PRG=$(SYSTEM_PROGRAM_DIR)/PS.PRG \
		--data-file KILL.PRG=$(SYSTEM_PROGRAM_DIR)/KILL.PRG \
		--data-file PWD.PRG=$(SYSTEM_PROGRAM_DIR)/PWD.PRG \
		--data-file SHELL.PRG=$(SYSTEM_PROGRAM_DIR)/SHELL.PRG \
		--data-file DESKTOP.PRG=$(SYSTEM_PROGRAM_DIR)/DESKTOP.PRG \
		--data-file MKDIR.PRG=$(SYSTEM_PROGRAM_DIR)/MKDIR.PRG \
		--data-file RMDIR.PRG=$(SYSTEM_PROGRAM_DIR)/RMDIR.PRG \
		--data-file DEL.PRG=$(SYSTEM_PROGRAM_DIR)/DEL.PRG \
		--data-file COPY.PRG=$(SYSTEM_PROGRAM_DIR)/COPY.PRG \
		--data-file ECHO.PRG=$(SYSTEM_PROGRAM_DIR)/ECHO.PRG \
		--data-file CLS.PRG=$(SYSTEM_PROGRAM_DIR)/CLS.PRG \
		--data-file DRIVES.PRG=$(SYSTEM_PROGRAM_DIR)/DRIVES.PRG \
		--data-file EDIT.PRG=$(SYSTEM_PROGRAM_DIR)/EDIT.PRG \
		--data-file CHILDEX.PRG=$(SYSTEM_PROGRAM_DIR)/CHILDEX.PRG \
		--data-file FAULTDE.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTDE.PRG \
		--data-file FAULTUD.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTUD.PRG \
		--data-file FAULTPF.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTPF.PRG \
		--data-file FAULTSTK.PRG=$(SYSTEM_PROGRAM_DIR)/FAULTSTK.PRG \
		--data-file GTEST.PRG=$(SYSTEM_PROGRAM_DIR)/GTEST.PRG \
		--data-file REIST.PRG=$(SYSTEM_PROGRAM_DIR)/REIST.PRG \
		--data-file STORAGE.PRG=$(SYSTEM_PROGRAM_DIR)/STORAGE.PRG \
		--data-file SLEEPER.PRG=$(SYSTEM_PROGRAM_DIR)/SLEEPER.PRG

# ============================================================================
# TESTING
# ============================================================================

# Fast, fixture-independent regression tests.
test: test-unit

test-unit:
	@echo "Running host-side unit tests..."
	@$(PYTHON) -m unittest discover -s test -p "test_*.py" -v

# Full integration tests additionally require generated disk images.
test-all: test-unit test-images

test-images:
	@echo "Running disk image unit tests..."
	@$(PYTHON) scripts/test_disk_images.py

test-smoke: native-image
	@echo "Running QEMU guest smoke test..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--log $(OUTPUT_DIR)/guest-smoke.log

test-smoke-pit: native-image
	@echo "Running QEMU guest smoke test with PIT scheduling..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--no-apic \
		--log $(OUTPUT_DIR)/guest-smoke-pit.log

test-smoke-watchdog: native-image
	@echo "Running QEMU guest smoke test with IB700 hardware watchdog..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--watchdog \
		--log $(OUTPUT_DIR)/guest-smoke-watchdog.log

test-smoke-journal-recovery: native-image
	@echo "Running REIST journal power-loss recovery test..."
	@$(PYTHON) scripts/test_journal_recovery.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--work-image $(OUTPUT_DIR)/journal-recovery.img \
		--log $(OUTPUT_DIR)/guest-smoke-journal-recovery.log

test-smoke-storage-recovery:
	@echo "Building isolated REIST storage-service crash injection image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga STORAGE_FAULT_INJECTION=1 \
		OUTPUT_DIR=build/storage-injection
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/storage-injection/reist-os.img \
		--expect-storage-recovery \
		--timeout 120 \
		--log build/storage-injection/guest-smoke-storage-recovery.log

test-smoke-storage-io-failure:
	@echo "Building isolated REIST storage I/O-failure injection image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga STORAGE_IO_FAULT_INJECTION=1 \
		OUTPUT_DIR=build/storage-io-injection
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/storage-io-injection/reist-os.img \
		--expect-storage-io-failure \
		--timeout 120 \
		--log build/storage-io-injection/guest-smoke-storage-io-failure.log

test-smoke-handover:
	@echo "Building isolated REIST external handover injection image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga HANDOVER_FAULT_INJECTION=1 \
		OUTPUT_DIR=build/handover-injection
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/handover-injection/reist-os.img \
		--expect-handover \
		--timeout 120 \
		--log build/handover-injection/guest-smoke-handover.log

test-smoke-handover-pair:
	@echo "Building REIST active handover channel..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga HANDOVER_FAULT_INJECTION=1 \
		HANDOVER_NODE_ID=1 OUTPUT_DIR=build/handover-active
	@echo "Building REIST standby handover channel..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga HANDOVER_FAULT_INJECTION=1 \
		HANDOVER_NODE_ID=2 OUTPUT_DIR=build/handover-standby
	@echo "Building REIST repaired standby handover channel..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga HANDOVER_FAULT_INJECTION=1 \
		HANDOVER_NODE_ID=3 OUTPUT_DIR=build/handover-rejoin
	@$(PYTHON) scripts/run_qemu_handover_pair.py \
		--qemu $(QEMU) \
		--active-image build/handover-active/reist-os.img \
		--standby-image build/handover-standby/reist-os.img \
		--rejoin-image build/handover-rejoin/reist-os.img \
		--timeout 120 \
		--log build/guest-smoke-handover-pair.log

test-smoke-fatal-recovery:
	@echo "Building isolated REIST Double-Fault injection image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga FAULT_INJECTION=1 \
		OUTPUT_DIR=build/fatal-injection
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/fatal-injection/reist-os.img \
		--watchdog \
		--expect-fatal-recovery \
		--timeout 120 \
		--log build/fatal-injection/guest-smoke-fatal-recovery.log

test-smoke-memory-fault:
	@echo "Building deterministic REIST memory-fault injection image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga MEMORY_FAULT_INJECTION=1 \
		OUTPUT_DIR=build/memory-fault-injection
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/memory-fault-injection/reist-os.img \
		--expect-memory-fault \
		--timeout 120 \
		--log build/memory-fault-injection/guest-smoke-memory-fault.log

test-smoke-memory: native-image
	@echo "Running QEMU guest smoke test with 32 MiB RAM..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--memory 32M \
		--log $(OUTPUT_DIR)/guest-smoke-memory-32m.log
	@echo "Running QEMU guest smoke test with 64 MiB RAM..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--memory 64M \
		--log $(OUTPUT_DIR)/guest-smoke-memory-64m.log
	@echo "Running QEMU guest smoke test with 256 MiB RAM..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--memory 256M \
		--log $(OUTPUT_DIR)/guest-smoke-memory-256m.log
	@echo "Running QEMU guest smoke test at the 1-GiB managed-RAM ceiling..."
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--memory 1024M \
		--log $(OUTPUT_DIR)/guest-smoke-memory-1024m.log

test-smoke-desktop:
	@echo "Building and running QEMU framebuffer desktop smoke test..."
	@$(MAKE) native-image TARGET=qemu VIDEO=framebuffer
	@$(PYTHON) scripts/run_qemu_desktop_smoke.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--log $(OUTPUT_DIR)/guest-smoke-desktop.log \
		--screenshot $(OUTPUT_DIR)/guest-smoke-desktop.ppm

# Run disk image tests with verbose output
test-verbose:
	@echo "Running disk image unit tests (verbose)..."
	@$(PYTHON) scripts/test_disk_images.py -v

# Run disk image tests (Bash version - no Python dependency)
test-bash:
	@echo "Running disk image unit tests (bash)..."
	@bash scripts/test_disk_images.sh

# Quick check: just verify images exist and are mountable
test-quick:
	@echo "Quick disk image check..."
	@for img in disk.img disk1.img floppy.img; do \
		if [ -f "$$img" ]; then \
			echo "✓ $$img exists ($$(du -h $$img | cut -f1))"; \
		else \
			echo "✗ $$img missing"; \
		fi; \
	done

# ============================================================================
# RUN IN QEMU
# ============================================================================

QEMU := qemu-system-i386
QEMU_IMAGE := -boot c -drive file=$(OUTPUT_DIR)/reist-os.img,format=raw,if=ide,index=0
QEMU_COMMON := -m 512M $(QEMU_IMAGE) -no-reboot -no-shutdown

run: run-native
run-disk: run-native

run-native: native-image
	@echo "Starting QEMU through the native BIOS/MBR path..."
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 \
		-vga std

# Rebuild the complete native image because the video mode affects both kernel
# objects and the boot-time framebuffer handoff.
run-fb:
	@$(MAKE) native-image TARGET=qemu VIDEO=framebuffer
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 \
		-vga std

run-debug: native-image
	@echo "Starting QEMU and waiting for GDB on TCP port 1234..."
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 \
		-s -S -vga std

run-floppy: floppy-image
	@$(QEMU) -m 512M -boot a \
		-drive file=$(OUTPUT_DIR)/reist-os-floppy.img,format=raw,if=floppy \
		-device rtl8139,netdev=net0 -netdev user,id=net0 -vga std \
		-no-reboot -no-shutdown

run-net-dump: native-image
	@echo "Writing network traffic to build/network-dump.pcap..."
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 \
		-object filter-dump,id=dump0,netdev=net0,file=$(OUTPUT_DIR)/network-dump.pcap \
		-vga std

run-rtl8139: native-image
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 -vga std

run-e1000: native-image
	@$(QEMU) $(QEMU_COMMON) \
		-device e1000,netdev=net0 -netdev user,id=net0 -vga std

run-ne2000: native-image
	@$(QEMU) $(QEMU_COMMON) \
		-device ne2k_pci,netdev=net0 -netdev user,id=net0 -vga std
# ============================================================================
# DEBUGGING INFO
# ============================================================================

print-vars:
	@echo "Architecture: $(ARCH)"
	@echo "Platform: $(PLATFORM)"
	@echo "Build directory: $(OUTPUT_DIR)"
	@echo ""
	@echo "Source files found:"
	@echo "  Architecture: $(words $(ARCH_BOOT_ASM) $(ARCH_CPU_ASM) $(ARCH_CPU_C) $(ARCH_MM_C)) files"
	@echo "  Kernel: $(words $(KERNEL_INIT_C) $(KERNEL_SYSCALL_C) $(KERNEL_PROC_C) $(KERNEL_SCHED_C) $(KERNEL_SCHED_ASM) $(KERNEL_TIME_C) $(KERNEL_SHELL_C)) files"
	@echo "  Memory: $(words $(MM_C)) files"
	@echo "  Filesystem: $(words $(FS_VFS_C) $(FS_FAT12_C) $(FS_FAT32_C)) files"
	@echo "  Drivers: $(words $(DRIVERS_BLOCK_C) $(DRIVERS_CHAR_C) $(DRIVERS_VIDEO_C) $(DRIVERS_NET_C) $(DRIVERS_BUS_C)) files"
	@echo "  Libraries: $(words $(LIB_LIBC_C) $(LIB_LIBC_ASM) $(LIB_LIBK_C)) files"
