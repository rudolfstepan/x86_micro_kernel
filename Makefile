# x86 Microkernel Makefile - Reorganized Structure
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
ISO_DIR := iso

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

# Compiler flags
ASFLAGS := -f elf32
CFLAGS := -m32 -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin \
          -O1 -Wall -Wextra -g -Wno-unused-parameter -Wno-unused-variable -U_FORTIFY_SOURCE \
          -I. -I$(ARCH_DIR)/include -Iinclude -I$(LIB_DIR)/libc

LDFLAGS := -m elf_i386 -nostdlib
KERNEL_LDSCRIPT := $(CONFIG_DIR)/klink.ld
USER_LDSCRIPT := $(CONFIG_DIR)/cli.ld

# Mount directory for disk image
MOUNT_DIR := /mnt/disk

# ============================================================================
# SOURCE FILES
# ============================================================================

# Architecture sources (x86)
ARCH_BOOT_ASM := $(wildcard $(ARCH_DIR)/boot/*.asm)
ARCH_CPU_ASM := $(wildcard $(ARCH_DIR)/cpu/*.asm)
ARCH_CPU_C := $(wildcard $(ARCH_DIR)/cpu/*.c)
ARCH_MM_C := $(wildcard $(ARCH_DIR)/mm/*.c)

# Kernel sources
KERNEL_INIT_C := $(wildcard $(KERNEL_DIR)/init/*.c)
KERNEL_SYSCALL_C := $(wildcard $(KERNEL_DIR)/syscall/*.c)
KERNEL_PROC_C := $(wildcard $(KERNEL_DIR)/proc/*.c)
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

# Driver sources
DRIVERS_BLOCK_C := $(wildcard $(DRIVERS_DIR)/block/*.c)
DRIVERS_CHAR_C := $(wildcard $(DRIVERS_DIR)/char/*.c)
DRIVERS_VIDEO_C := $(wildcard $(DRIVERS_DIR)/video/*.c)
DRIVERS_NET_C := $(wildcard $(DRIVERS_DIR)/net/*.c)
DRIVERS_BUS_C := $(wildcard $(DRIVERS_DIR)/bus/*.c)

# Library sources
LIB_LIBC_C := $(wildcard $(LIB_DIR)/libc/*.c)
LIB_LIBC_ASM := $(wildcard $(LIB_DIR)/libc/*.asm)
LIB_LIBK_C := $(wildcard $(LIB_DIR)/libk/*.c)

# Userspace programs
USERSPACE_C := $(wildcard $(USERSPACE_DIR)/bin/*.c)

# ============================================================================
# OBJECT FILES
# ============================================================================

# Architecture objects
ARCH_BOOT_OBJ := $(patsubst $(ARCH_DIR)/boot/%.asm,$(BUILD_ARCH_DIR)/boot/%.o,$(ARCH_BOOT_ASM))
ARCH_CPU_ASM_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.asm,$(BUILD_ARCH_DIR)/cpu/%_asm.o,$(ARCH_CPU_ASM))
ARCH_CPU_C_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.c,$(BUILD_ARCH_DIR)/cpu/%.o,$(ARCH_CPU_C))
ARCH_MM_OBJ := $(patsubst $(ARCH_DIR)/mm/%.c,$(BUILD_ARCH_DIR)/mm/%.o,$(ARCH_MM_C))

# Kernel objects
KERNEL_INIT_OBJ := $(patsubst $(KERNEL_DIR)/init/%.c,$(BUILD_KERNEL_DIR)/init/%.o,$(KERNEL_INIT_C))
KERNEL_SYSCALL_OBJ := $(patsubst $(KERNEL_DIR)/syscall/%.c,$(BUILD_KERNEL_DIR)/syscall/%.o,$(KERNEL_SYSCALL_C))
KERNEL_PROC_OBJ := $(patsubst $(KERNEL_DIR)/proc/%.c,$(BUILD_KERNEL_DIR)/proc/%.o,$(KERNEL_PROC_C))
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

# Driver objects
DRIVERS_BLOCK_OBJ := $(patsubst $(DRIVERS_DIR)/block/%.c,$(BUILD_DRIVERS_DIR)/block/%.o,$(DRIVERS_BLOCK_C))
DRIVERS_CHAR_OBJ := $(patsubst $(DRIVERS_DIR)/char/%.c,$(BUILD_DRIVERS_DIR)/char/%.o,$(DRIVERS_CHAR_C))
DRIVERS_VIDEO_OBJ := $(patsubst $(DRIVERS_DIR)/video/%.c,$(BUILD_DRIVERS_DIR)/video/%.o,$(DRIVERS_VIDEO_C))
DRIVERS_NET_OBJ := $(patsubst $(DRIVERS_DIR)/net/%.c,$(BUILD_DRIVERS_DIR)/net/%.o,$(DRIVERS_NET_C))
DRIVERS_BUS_OBJ := $(patsubst $(DRIVERS_DIR)/bus/%.c,$(BUILD_DRIVERS_DIR)/bus/%.o,$(DRIVERS_BUS_C))

# Library objects
LIB_LIBC_C_OBJ := $(patsubst $(LIB_DIR)/libc/%.c,$(BUILD_LIB_DIR)/libc/%.o,$(LIB_LIBC_C))
LIB_LIBC_ASM_OBJ := $(patsubst $(LIB_DIR)/libc/%.asm,$(BUILD_LIB_DIR)/libc/%.o,$(LIB_LIBC_ASM))
LIB_LIBK_OBJ := $(patsubst $(LIB_DIR)/libk/%.c,$(BUILD_LIB_DIR)/libk/%.o,$(LIB_LIBK_C))

# Aggregate objects for linking
ARCH_OBJ := $(ARCH_BOOT_OBJ) $(ARCH_CPU_ASM_OBJ) $(ARCH_CPU_C_OBJ) $(ARCH_MM_OBJ)
KERNEL_OBJ := $(KERNEL_INIT_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_PROC_OBJ) \
              $(KERNEL_SCHED_C_OBJ) $(KERNEL_SCHED_ASM_OBJ) $(KERNEL_TIME_OBJ) \
              $(KERNEL_SHELL_OBJ)
FS_OBJ := $(FS_VFS_OBJ) $(FS_FAT12_OBJ) $(FS_FAT32_OBJ)
DRIVERS_OBJ := $(DRIVERS_BLOCK_OBJ) $(DRIVERS_CHAR_OBJ) $(DRIVERS_VIDEO_OBJ) \
               $(DRIVERS_NET_OBJ) $(DRIVERS_BUS_OBJ)
LIB_OBJ := $(LIB_LIBC_C_OBJ) $(LIB_LIBC_ASM_OBJ) $(LIB_LIBK_OBJ)

ALL_OBJ := $(ARCH_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(FS_OBJ) $(DRIVERS_OBJ) $(LIB_OBJ)

# ============================================================================
# TARGETS
# ============================================================================

.PHONY: all clean prepare kernel iso run help

all: prepare kernel iso

help:
	@echo "x86 Microkernel Build System"
	@echo "============================"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build kernel and create ISO (default)"
	@echo "  kernel    - Build kernel binary only"
	@echo "  iso       - Create bootable ISO image"
	@echo "  clean     - Remove all build artifacts"
	@echo "  run       - Build and run in QEMU"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Directory Structure:"
	@echo "  arch/     - Architecture-specific code (x86)"
	@echo "  kernel/   - Core kernel (init, syscall, sched, proc, time, shell)"
	@echo "  mm/       - Memory management"
	@echo "  fs/       - Filesystem (VFS, FAT12, FAT32)"
	@echo "  drivers/  - Device drivers (block, char, video, net, bus)"
	@echo "  lib/      - Libraries (libc, libk)"

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(OUTPUT_DIR)/* $(ISO_DIR)/*

prepare:
	@echo "Creating build directories..."
	@mkdir -p $(BUILD_ARCH_DIR)/boot $(BUILD_ARCH_DIR)/cpu $(BUILD_ARCH_DIR)/mm
	@mkdir -p $(BUILD_KERNEL_DIR)/init $(BUILD_KERNEL_DIR)/syscall $(BUILD_KERNEL_DIR)/proc
	@mkdir -p $(BUILD_KERNEL_DIR)/sched $(BUILD_KERNEL_DIR)/time $(BUILD_KERNEL_DIR)/shell
	@mkdir -p $(BUILD_MM_DIR)
	@mkdir -p $(BUILD_FS_DIR)/vfs $(BUILD_FS_DIR)/fat12 $(BUILD_FS_DIR)/fat32
	@mkdir -p $(BUILD_DRIVERS_DIR)/block $(BUILD_DRIVERS_DIR)/char $(BUILD_DRIVERS_DIR)/video
	@mkdir -p $(BUILD_DRIVERS_DIR)/net $(BUILD_DRIVERS_DIR)/bus
	@mkdir -p $(BUILD_LIB_DIR)/libc $(BUILD_LIB_DIR)/libk
	@mkdir -p $(BUILD_USERSPACE_DIR)/bin

# ============================================================================
# COMPILATION RULES
# ============================================================================

# Architecture - Boot assembly
$(BUILD_ARCH_DIR)/boot/%.o: $(ARCH_DIR)/boot/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Architecture - CPU assembly (renamed to avoid conflicts with C files)
$(BUILD_ARCH_DIR)/cpu/%_asm.o: $(ARCH_DIR)/cpu/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Architecture - CPU C
$(BUILD_ARCH_DIR)/cpu/%.o: $(ARCH_DIR)/cpu/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Architecture - Memory management
$(BUILD_ARCH_DIR)/mm/%.o: $(ARCH_DIR)/mm/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Init
$(BUILD_KERNEL_DIR)/init/%.o: $(KERNEL_DIR)/init/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Syscall
$(BUILD_KERNEL_DIR)/syscall/%.o: $(KERNEL_DIR)/syscall/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Process
$(BUILD_KERNEL_DIR)/proc/%.o: $(KERNEL_DIR)/proc/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Scheduler C
$(BUILD_KERNEL_DIR)/sched/%.o: $(KERNEL_DIR)/sched/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Scheduler ASM
$(BUILD_KERNEL_DIR)/sched/%.o: $(KERNEL_DIR)/sched/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Kernel - Time
$(BUILD_KERNEL_DIR)/time/%.o: $(KERNEL_DIR)/time/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Kernel - Shell
$(BUILD_KERNEL_DIR)/shell/%.o: $(KERNEL_DIR)/shell/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Memory management
$(BUILD_MM_DIR)/%.o: $(MM_DIR)/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Filesystem - VFS
$(BUILD_FS_DIR)/vfs/%.o: $(FS_DIR)/vfs/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Filesystem - FAT12
$(BUILD_FS_DIR)/fat12/%.o: $(FS_DIR)/fat12/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Filesystem - FAT32
$(BUILD_FS_DIR)/fat32/%.o: $(FS_DIR)/fat32/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Drivers - Block
$(BUILD_DRIVERS_DIR)/block/%.o: $(DRIVERS_DIR)/block/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Drivers - Char
$(BUILD_DRIVERS_DIR)/char/%.o: $(DRIVERS_DIR)/char/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Drivers - Video
$(BUILD_DRIVERS_DIR)/video/%.o: $(DRIVERS_DIR)/video/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Drivers - Net
$(BUILD_DRIVERS_DIR)/net/%.o: $(DRIVERS_DIR)/net/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Drivers - Bus
$(BUILD_DRIVERS_DIR)/bus/%.o: $(DRIVERS_DIR)/bus/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Library - libc C
$(BUILD_LIB_DIR)/libc/%.o: $(LIB_DIR)/libc/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# Library - libc ASM
$(BUILD_LIB_DIR)/libc/%.o: $(LIB_DIR)/libc/%.asm
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Library - libk
$(BUILD_LIB_DIR)/libk/%.o: $(LIB_DIR)/libk/%.c
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) $< -o $@

# ============================================================================
# KERNEL LINKING
# ============================================================================

kernel: $(ALL_OBJ)
	@echo "Linking kernel..."
	@$(LD) $(LDFLAGS) -T $(KERNEL_LDSCRIPT) -o $(OUTPUT_DIR)/kernel.bin $(ALL_OBJ)
	@echo "Kernel built successfully: $(OUTPUT_DIR)/kernel.bin"

# ============================================================================
# ISO CREATION
# ============================================================================

iso: kernel
	@echo "Creating bootable ISO..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(OUTPUT_DIR)/kernel.bin $(ISO_DIR)/boot/
	@cp $(CONFIG_DIR)/grub.cfg $(ISO_DIR)/boot/grub/
	@grub-mkrescue -o kernel.iso $(ISO_DIR) 2>/dev/null || \
		(echo "Error: grub-mkrescue failed. Install grub-pc-bin or grub2-common" && exit 1)
	@echo "ISO created: kernel.iso"

# ============================================================================
# RUN IN QEMU
# ============================================================================

run: iso
	@echo "Starting QEMU..."
	@qemu-system-x86_64 -m 512M -boot d -cdrom ./kernel.iso \
		-drive file=./disk.img,format=raw \
		-drive file=./floppy.img,format=raw,if=floppy \
		-device ne2k_pci,netdev=net0 -netdev user,id=net0 \
		-monitor stdio -vga vmware

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
