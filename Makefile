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
OPENSSL ?= openssl
ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
ZIG ?= zig
USER_PROGRAM_SOURCE ?= userspace/programs/hello.c
USER_PROGRAM_OUTPUT ?= $(OUTPUT_DIR)/programs/HELLO.PRG
USER_PROGRAM_LIBS ?=
USER_SDK_DIR ?= $(OUTPUT_DIR)/sdk
SYSTEM_PROGRAM_DIR := $(OUTPUT_DIR)/programs
BOOT_SIGNATURE := $(OUTPUT_DIR)/kernel.bin.sig
BOOT_TRUST_POLICY := safety/boot_trust_policy.json
BOOT_PUBLIC_KEY := safety/keys/reist-research-dev-public.pem
BOOT_PRIVATE_KEY := test/fixtures/reist-research-dev-private.pem

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
AHCI_FAULT_INJECTION ?= 0
AHCI_FAULT_MODE ?= timeout
HANDOVER_FAULT_INJECTION ?= 0
DHCP_LEASE_FAULT_INJECTION ?= 0
DHCP_RENEW_FAULT_INJECTION ?= 0
HANDOVER_NODE_ID ?= 0
VBE_RUNTIME_TEST ?= 0
DRIVER_DOMAIN_FAULT_INJECTION ?= 0
SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION ?= 0
HDA_SMP_LIFECYCLE_FAULT_INJECTION ?= 0
AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION ?= 0
COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION ?= 0
SOUNDPLAYER_SURFACE_PROBE ?= 0
COMPOSITOR_HOVER_PROBE ?= 0
RUNTIME_DEGRADATION_FAULT_INJECTION ?= 0
RESILIENT_PAGE_BOOT_PROOF ?= 0

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
ifeq ($(AHCI_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_AHCI_FAULT_INJECTION
    ifeq ($(AHCI_FAULT_MODE),tfes)
        SAFETY_TEST_DEFINES += -DREIST_AHCI_FAULT_TFES
    else ifeq ($(AHCI_FAULT_MODE),tfd)
        SAFETY_TEST_DEFINES += -DREIST_AHCI_FAULT_TFD
    else
        SAFETY_TEST_DEFINES += -DREIST_AHCI_FAULT_TIMEOUT
    endif
endif

ifeq ($(HANDOVER_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_HANDOVER_FAULT_INJECTION \
        -DREIST_HANDOVER_NODE_ID=$(HANDOVER_NODE_ID)
endif

ifeq ($(DHCP_LEASE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_DHCP_LEASE_TEST_MS=2500U
endif

ifeq ($(VBE_RUNTIME_TEST),1)
    SAFETY_TEST_DEFINES += -DREIST_VBE_RUNTIME_TEST
endif

ifeq ($(DRIVER_DOMAIN_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_DRIVER_DOMAIN_FAULT_INJECTION
endif

ifeq ($(SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION
endif

ifeq ($(HDA_SMP_LIFECYCLE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_HDA_SMP_LIFECYCLE_FAULT_INJECTION
endif
ifeq ($(AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION
endif
ifeq ($(COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION
endif
ifeq ($(SOUNDPLAYER_SURFACE_PROBE),1)
    SAFETY_TEST_DEFINES += -DREIST_SOUNDPLAYER_SURFACE_PROBE
endif
ifeq ($(COMPOSITOR_HOVER_PROBE),1)
    SAFETY_TEST_DEFINES += -DREIST_COMPOSITOR_HOVER_PROBE
endif

ifeq ($(RUNTIME_DEGRADATION_FAULT_INJECTION),1)
    SAFETY_TEST_DEFINES += -DREIST_RUNTIME_DEGRADATION_FAULT_INJECTION
endif

ifeq ($(RESILIENT_PAGE_BOOT_PROOF),1)
    SAFETY_TEST_DEFINES += -DREIST_RESILIENT_PAGE_BOOT_PROOF
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

FRAME_WARNING_FLAGS ?= -Wframe-larger-than=4096 -Werror=frame-larger-than=4096
CFLAGS := -m32 -std=gnu11 -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
          -fno-builtin -fno-pic -fno-pie -fno-stack-protector \
          -Werror=vla $(FRAME_WARNING_FLAGS) \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -msoft-float \
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

# Isolated architecture bootstrap.  This target deliberately does not feed
# objects or flags into the production i386 kernel/image dependency graph.
X86_64_BOOTSTRAP_DIR := $(OUTPUT_DIR)/x86_64
X86_64_BOOTSTRAP_OBJ := $(X86_64_BOOTSTRAP_DIR)/entry.o
X86_64_EXCEPTION_OBJ := $(X86_64_BOOTSTRAP_DIR)/exceptions.o
X86_64_TIMER_INTERRUPT_OBJ := $(X86_64_BOOTSTRAP_DIR)/timer_interrupt.o
X86_64_PHYSICAL_MEMORY_OBJ := $(X86_64_BOOTSTRAP_DIR)/physical_memory.o
X86_64_ELF64_LOADER_OBJ := $(X86_64_BOOTSTRAP_DIR)/elf64_loader.o
X86_64_USER_EXECUTION_OBJ := $(X86_64_BOOTSTRAP_DIR)/user_execution.o
X86_64_PROCESS_SCHEDULER_OBJ := $(X86_64_BOOTSTRAP_DIR)/cooperative_scheduler.o
X86_64_C_CORE_OBJ := $(X86_64_BOOTSTRAP_DIR)/bootstrap_core.o
X86_64_C_CORE_ELF := $(X86_64_BOOTSTRAP_DIR)/reist-x86_64-c-core.elf
X86_64_C_CORE_TEXT := $(X86_64_BOOTSTRAP_DIR)/bootstrap_core_text.bin
X86_64_C_CORE_RODATA := $(X86_64_BOOTSTRAP_DIR)/bootstrap_core_rodata.bin
X86_64_C_CORE_DATA := $(X86_64_BOOTSTRAP_DIR)/bootstrap_core_data.bin
X86_64_USER_PROBE_OBJ := $(X86_64_BOOTSTRAP_DIR)/user_probe.o
X86_64_USER_PROBE_ELF := $(X86_64_BOOTSTRAP_DIR)/reist-x86_64-user-probe.elf
X86_64_USER_SHELL_OBJ := $(X86_64_BOOTSTRAP_DIR)/user_shell.o
X86_64_USER_SHELL_ELF := $(X86_64_BOOTSTRAP_DIR)/reist-x86_64-user-shell.elf
X86_64_USER_CHILD_OBJ := $(X86_64_BOOTSTRAP_DIR)/user_child.o
X86_64_USER_CHILD_ELF := $(X86_64_BOOTSTRAP_DIR)/reist-x86_64-user-child.elf
X86_64_BOOTSTRAP_ELF := $(X86_64_BOOTSTRAP_DIR)/reist-x86_64-bootstrap.elf
X86_64_BOOTSTRAP_LDSCRIPT := $(CONFIG_DIR)/x86_64_bootstrap.ld
X86_64_CC ?= $(CC)
X86_64_CFLAGS := -target x86_64-freestanding-none -std=c11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mno-red-zone \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
	-fno-pic -fno-pie -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
	-fno-vectorize -fno-slp-vectorize -fno-stack-check -Werror=vla -g0
X86_64_USER_CFLAGS := -target x86_64-freestanding-none -std=c11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -nostdlib -fno-builtin -fno-stack-protector -mno-red-zone \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
	-fno-pic -fno-pie -mcmodel=small -mno-mmx -mno-sse -mno-sse2 \
	-fno-vectorize -fno-slp-vectorize -fno-stack-check -Werror=vla -g0

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
ARCH_PLATFORM_C := $(wildcard $(ARCH_DIR)/platform/*.c)

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

CONFIG_VARIANT := f$(FAULT_INJECTION)-m$(MEMORY_FAULT_INJECTION)-s$(STORAGE_FAULT_INJECTION)-io$(STORAGE_IO_FAULT_INJECTION)-a$(AHCI_FAULT_INJECTION)-$(AHCI_FAULT_MODE)-h$(HANDOVER_FAULT_INJECTION)-n$(HANDOVER_NODE_ID)-dl$(DHCP_LEASE_FAULT_INJECTION)-dr$(DHCP_RENEW_FAULT_INJECTION)-v$(VBE_RUNTIME_TEST)-dd$(DRIVER_DOMAIN_FAULT_INJECTION)-svs$(SVGA2D_SMP_LIFECYCLE_FAULT_INJECTION)-hds$(HDA_SMP_LIFECYCLE_FAULT_INJECTION)-ass$(AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION)-csl$(COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION)-sap$(SOUNDPLAYER_SURFACE_PROBE)-chp$(COMPOSITOR_HOVER_PROBE)-rd$(RUNTIME_DEGRADATION_FAULT_INJECTION)-rp$(RESILIENT_PAGE_BOOT_PROOF)
CONFIG_STAMP := $(OUTPUT_DIR)/.config-$(TARGET)-$(VIDEO)-$(CONFIG_VARIANT)

# ============================================================================
# OBJECT FILES
# ============================================================================

# Architecture objects
ARCH_BOOT_ASM_OBJ := $(patsubst $(ARCH_DIR)/boot/%.asm,$(BUILD_ARCH_DIR)/boot/%.o,$(ARCH_BOOT_ASM))
ARCH_BOOT_C_OBJ := $(patsubst $(ARCH_DIR)/boot/%.c,$(BUILD_ARCH_DIR)/boot/%.o,$(ARCH_BOOT_C))
ARCH_CPU_ASM_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.asm,$(BUILD_ARCH_DIR)/cpu/%_asm.o,$(ARCH_CPU_ASM))
ARCH_CPU_C_OBJ := $(patsubst $(ARCH_DIR)/cpu/%.c,$(BUILD_ARCH_DIR)/cpu/%.o,$(ARCH_CPU_C))
ARCH_MM_OBJ := $(patsubst $(ARCH_DIR)/mm/%.c,$(BUILD_ARCH_DIR)/mm/%.o,$(ARCH_MM_C))
ARCH_PLATFORM_OBJ := $(patsubst $(ARCH_DIR)/platform/%.c,$(BUILD_ARCH_DIR)/platform/%.o,$(ARCH_PLATFORM_C))

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
ARCH_OBJ := $(ARCH_BOOT_ASM_OBJ) $(ARCH_BOOT_C_OBJ) $(ARCH_CPU_ASM_OBJ) $(ARCH_CPU_C_OBJ) $(ARCH_MM_OBJ) $(ARCH_PLATFORM_OBJ)
KERNEL_OBJ := $(KERNEL_INIT_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_PROC_OBJ) \
              $(KERNEL_IPC_OBJ) \
              $(KERNEL_SCHED_C_OBJ) $(KERNEL_SCHED_ASM_OBJ) $(KERNEL_TIME_OBJ) \
              $(KERNEL_SHELL_OBJ)
FS_OBJ := $(FS_VFS_OBJ) $(FS_FAT12_OBJ) $(FS_FAT32_OBJ) $(FS_EXT2_OBJ)
DRIVERS_OBJ := $(DRIVERS_BLOCK_OBJ) $(DRIVERS_CHAR_OBJ) $(DRIVERS_VIDEO_OBJ) \
               $(DRIVERS_NET_OBJ) $(DRIVERS_USB_OBJ) $(DRIVERS_BUS_OBJ)
LIB_OBJ := $(LIB_LIBC_C_OBJ) $(LIB_LIBC_ASM_OBJ) $(LIB_LIBK_OBJ)

ALL_OBJ := $(ARCH_OBJ) $(KERNEL_OBJ) $(MM_OBJ) $(FS_OBJ) $(DRIVERS_OBJ) $(LIB_OBJ)
C_OBJ := $(ARCH_BOOT_C_OBJ) $(ARCH_CPU_C_OBJ) $(ARCH_MM_OBJ) $(ARCH_PLATFORM_OBJ) \
         $(KERNEL_INIT_OBJ) $(KERNEL_SYSCALL_OBJ) $(KERNEL_PROC_OBJ) \
         $(KERNEL_IPC_OBJ) $(KERNEL_SCHED_C_OBJ) $(KERNEL_TIME_OBJ) \
         $(KERNEL_SHELL_OBJ) $(MM_OBJ) $(FS_OBJ) $(DRIVERS_OBJ) \
         $(LIB_LIBC_C_OBJ) $(LIB_LIBK_OBJ)
DEPS := $(C_OBJ:.o=.d)

# Ensure the generated directory tree exists before any object is built.
# This also makes direct and parallel `make kernel` invocations reliable.
$(ALL_OBJ): $(CONFIG_STAMP) | prepare check-syscall-abi

# Object paths are shared across configurations.  A target/video switch must
# therefore invalidate all objects even when no source timestamp changed.
$(CONFIG_STAMP):
	@$(PYTHON) -c "from pathlib import Path; p=Path('$(OUTPUT_DIR)'); p.mkdir(parents=True, exist_ok=True); [f.unlink() for f in p.glob('.config-*')]; Path('$@').touch()"

-include $(DEPS)

# ============================================================================
# TARGETS
# ============================================================================

.PHONY: all clean prepare kernel signed-kernel check-syscall-abi check-kernel-dependencies check-kernel-stack check-kernel-stack-analysis user-sdk user-program system-programs bootdisk native-image floppy-image x86_64-bootstrap run run-disk run-native run-usb run-floppy run-fb help format-disks test test-unit test-desktop-host test-all test-images test-smoke test-smoke-pit test-smoke-watchdog test-smoke-fatal-recovery test-smoke-memory-fault test-smoke-runtime-degradation test-smoke-journal-recovery test-smoke-storage-recovery test-smoke-storage-io-failure test-smoke-fdd-hotplug test-smoke-handover test-smoke-handover-pair test-smoke-memory test-smoke-desktop test-verbose test-bash test-quick run-debug print-vars build-qemu build-qemu-fb build-vmware build-real-hw clean-all

all: native-image

x86_64-bootstrap:
	@mkdir -p $(X86_64_BOOTSTRAP_DIR)
	@$(AS) -f elf64 arch/x86_64/user/probe.asm -o $(X86_64_USER_PROBE_OBJ)
	@$(LD) -m elf_x86_64 -nostdlib --build-id=none --fatal-warnings \
		-T config/x86_64_user_probe.ld -o $(X86_64_USER_PROBE_ELF) $(X86_64_USER_PROBE_OBJ)
	@$(X86_64_CC) $(X86_64_USER_CFLAGS) -c arch/x86_64/user/shell.c \
		-o $(X86_64_USER_SHELL_OBJ)
	@$(LD) -m elf_x86_64 -nostdlib --build-id=none --fatal-warnings --no-undefined \
		-z noexecstack --strip-all -T config/x86_64_user_shell.ld \
		-o $(X86_64_USER_SHELL_ELF) $(X86_64_USER_SHELL_OBJ)
	@$(AS) -f elf64 arch/x86_64/user/child.asm -o $(X86_64_USER_CHILD_OBJ)
	@$(LD) -m elf_x86_64 -nostdlib --build-id=none --fatal-warnings --no-undefined \
		-z noexecstack --strip-all -T config/x86_64_user_child.ld \
		-o $(X86_64_USER_CHILD_ELF) $(X86_64_USER_CHILD_OBJ)
	@$(X86_64_CC) $(X86_64_CFLAGS) -Iarch/x86_64/kernel -c \
		arch/x86_64/kernel/bootstrap_core.c -o $(X86_64_C_CORE_OBJ)
	@$(LD) -m elf_x86_64 -nostdlib --build-id=none --fatal-warnings --no-undefined \
		-z noexecstack --strip-debug -e x86_64_c_core_entry \
		--section-start=.text=0xFFFFFFFF80185000 \
		--section-start=.rodata=0xFFFFFFFF80186000 \
		--section-start=.data=0xFFFFFFFF80187000 \
		--section-start=.bss=0xFFFFFFFF80188000 \
		--defsym=x86_64_c_serial_write64=0xFFFFFFFF80184000 \
		--defsym=x86_64_c_process_shell64=0xFFFFFFFF80184100 \
		--defsym=x86_64_c_handoff=0xFFFFFFFF80188020 \
		--defsym=x86_64_c_control_handoff=0xFFFFFFFF801880A0 \
		-o $(X86_64_C_CORE_ELF) $(X86_64_C_CORE_OBJ)
	@$(OBJCOPY) -O binary --only-section=.text $(X86_64_C_CORE_ELF) $(X86_64_C_CORE_TEXT)
	@$(OBJCOPY) -O binary --only-section=.rodata $(X86_64_C_CORE_ELF) $(X86_64_C_CORE_RODATA)
	@$(OBJCOPY) -O binary --only-section=.data $(X86_64_C_CORE_ELF) $(X86_64_C_CORE_DATA)
	@$(AS) -f elf32 -DC_CORE_TEXT_PATH=\"$(X86_64_C_CORE_TEXT)\" \
		-DC_CORE_RODATA_PATH=\"$(X86_64_C_CORE_RODATA)\" \
		-DC_CORE_DATA_PATH=\"$(X86_64_C_CORE_DATA)\" \
		arch/x86_64/boot/entry.asm -o $(X86_64_BOOTSTRAP_OBJ)
	@$(AS) -f elf32 arch/x86_64/cpu/exceptions.asm -o $(X86_64_EXCEPTION_OBJ)
	@$(AS) -f elf32 arch/x86_64/cpu/timer_interrupt.asm -o $(X86_64_TIMER_INTERRUPT_OBJ)
	@$(AS) -f elf32 arch/x86_64/mm/physical_memory.asm -o $(X86_64_PHYSICAL_MEMORY_OBJ)
	@$(AS) -f elf32 -DUSER_PROBE_PATH=\"$(X86_64_USER_PROBE_ELF)\" \
		-DUSER_SHELL_PATH=\"$(X86_64_USER_SHELL_ELF)\" \
		-DUSER_CHILD_PATH=\"$(X86_64_USER_CHILD_ELF)\" \
		arch/x86_64/exec/elf64_loader.asm -o $(X86_64_ELF64_LOADER_OBJ)
	@$(AS) -f elf32 arch/x86_64/proc/user_execution.asm -o $(X86_64_USER_EXECUTION_OBJ)
	@$(AS) -f elf32 arch/x86_64/proc/cooperative_scheduler.asm -o $(X86_64_PROCESS_SCHEDULER_OBJ)
	@$(LD) -m elf_i386 -nostdlib --build-id=none --fatal-warnings \
		-T $(X86_64_BOOTSTRAP_LDSCRIPT) -o $(X86_64_BOOTSTRAP_ELF) \
		$(X86_64_BOOTSTRAP_OBJ) $(X86_64_EXCEPTION_OBJ) $(X86_64_TIMER_INTERRUPT_OBJ) \
		$(X86_64_PHYSICAL_MEMORY_OBJ) \
		$(X86_64_ELF64_LOADER_OBJ) $(X86_64_USER_EXECUTION_OBJ) \
		$(X86_64_PROCESS_SCHEDULER_OBJ)
	@echo "x86_64 bootstrap complete: $(X86_64_BOOTSTRAP_ELF)"

check-syscall-abi:
	@$(PYTHON) scripts/generate_syscall_abi.py --check

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
	@echo "  x86_64-bootstrap - Build the isolated IA-32e transition proof"
	@echo "  user-sdk     - Build conventional REIST headers and static libraries"
	@echo "  user-program - Compile USER_PROGRAM_SOURCE into a loadable MYPR file"
	@echo "  system-programs - Build the native Ring-3 system tool collection"
	@echo "  bootdisk     - Create native BIOS disk image"
	@echo "  floppy-image - Create a 1.44-MB BIOS floppy image"
	@echo "  clean        - Remove all build artifacts"
	@echo ""
	@echo "Run Targets:"
	@echo "  run          - Build and run the native image in QEMU"
	@echo "  run-disk     - Build and run in QEMU (from bootable disk)"
	@echo "  run-usb      - Build and run QEMU with xHCI and USB keyboard"
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
	@echo "  test-smoke-fdd-hotplug - Eject and reconnect A: while the guest runs"
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
	@$(PYTHON) -c "from pathlib import Path; [Path(p).mkdir(parents=True, exist_ok=True) for p in '$(BUILD_ARCH_DIR)/boot $(BUILD_ARCH_DIR)/cpu $(BUILD_ARCH_DIR)/mm $(BUILD_ARCH_DIR)/platform $(BUILD_KERNEL_DIR)/init $(BUILD_KERNEL_DIR)/syscall $(BUILD_KERNEL_DIR)/proc $(BUILD_KERNEL_DIR)/ipc $(BUILD_KERNEL_DIR)/sched $(BUILD_KERNEL_DIR)/time $(BUILD_KERNEL_DIR)/shell $(BUILD_MM_DIR) $(BUILD_FS_DIR)/vfs $(BUILD_FS_DIR)/fat12 $(BUILD_FS_DIR)/fat32 $(BUILD_FS_DIR)/ext2 $(BUILD_DRIVERS_DIR)/block $(BUILD_DRIVERS_DIR)/char $(BUILD_DRIVERS_DIR)/video $(BUILD_DRIVERS_DIR)/net $(BUILD_DRIVERS_DIR)/bus $(BUILD_DRIVERS_DIR)/usb $(BUILD_LIB_DIR)/libc $(BUILD_LIB_DIR)/libk $(BUILD_USERSPACE_DIR)/bin'.split()]"

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

# Architecture - platform firmware discovery
$(BUILD_ARCH_DIR)/platform/%.o: $(ARCH_DIR)/platform/%.c
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

kernel: check-syscall-abi check-kernel-dependencies $(OUTPUT_DIR)/kernel.bin

$(OUTPUT_DIR)/kernel.bin: $(ALL_OBJ) $(KERNEL_LDSCRIPT)
	@echo "Linking kernel..."
	@$(LD) $(LDFLAGS) -T $(KERNEL_LDSCRIPT) -o $(OUTPUT_DIR)/kernel.bin $(ALL_OBJ)
	@echo "Release kernel ELF: $(OUTPUT_DIR)/kernel.bin"

check-kernel-stack: check-kernel-dependencies
	@echo "Kernel stack-frame and VLA compiler gates passed."

check-kernel-stack-c-objects: $(C_OBJ)
	@echo "C-only stack-analysis objects built: $(words $(C_OBJ))"

check-kernel-stack-analysis:
	@$(MAKE) clean OUTPUT_DIR=$(STACK_ANALYSIS_OUTPUT_DIR)
	@$(MAKE) check-kernel-stack-c-objects OUTPUT_DIR=$(STACK_ANALYSIS_OUTPUT_DIR) \
		CC=$(STACK_ANALYSIS_CC) \
		FRAME_WARNING_FLAGS="-Wframe-larger-than=4096 -Werror=frame-larger-than=4096" \
		STACK_ANALYSIS_FLAGS="-fstack-usage -fcallgraph-info=su"
	@$(PYTHON) scripts/validate_stack_usage.py \
		--root $(STACK_ANALYSIS_OUTPUT_DIR) --expected $(words $(C_OBJ)) \
		--local-limit 4096 --budget-file safety/stack_budgets.json \
		--source-root .

# ============================================================================
# BOOTABLE DISK IMAGE
# ============================================================================

SYSTEM_IMAGE_FILES := \
	etc/reist/system.conf=config/etc/reist/system.conf \
	etc/reist/input.conf=config/etc/reist/input.conf \
	etc/reist/desktop.conf=config/etc/reist/desktop.conf \
	etc/reist/desktop-layout.conf=config/etc/reist/desktop-layout.conf \
	etc/reist/filetypes.conf=config/etc/reist/filetypes.conf \
	etc/reist/sounds.conf=config/etc/reist/sounds.conf \
	usr/share/icons/folder-empty.ico=assets/icons/folder-empty.ico \
	usr/share/icons/folder-full.ico=assets/icons/folder-full.ico \
	usr/share/icons/program.ico=assets/icons/program.ico \
	usr/share/icons/text.ico=assets/icons/text.ico \
	usr/share/icons/audio.ico=assets/icons/audio.ico \
	usr/share/icons/image.ico=assets/icons/image.ico \
	usr/share/icons/settings.ico=assets/icons/settings.ico \
	usr/share/icons/unknown.ico=assets/icons/unknown.ico \
	usr/share/icons/trash-empty.ico=assets/icons/trash-empty.ico \
	usr/share/icons/trash-full.ico=assets/icons/trash-full.ico \
	usr/share/icons/shortcut.ico=assets/icons/shortcut.ico \
	usr/share/sounds/startup.wav=assets/audio/startup.wav \
	usr/share/sounds/shutdown.wav=assets/audio/shutdown.wav \
	usr/share/sounds/error.wav=assets/audio/error.wav \
	usr/share/sounds/notify.wav=assets/audio/notify.wav \
	usr/share/sounds/trash-drop.wav=assets/audio/trash-drop.wav \
	usr/share/sounds/trash-empty.wav=assets/audio/trash-empty.wav \
	usr/share/images/reist-splash.bmp=assets/images/reist-splash.bmp \
	usr/share/images/demo-desktop.bmp=assets/images/demo-desktop.bmp \
	usr/share/images/demo-colors.gif=assets/images/demo-colors.gif \
	usr/share/fonts/reist-vga.psf=assets/fonts/reist-vga.psf \
	usr/share/fonts/reist-unicode.psf=assets/fonts/reist-unicode.psf \
	usr/share/fonts/reist-jetbrains-mono-10.psf=assets/fonts/reist-jetbrains-mono-10.psf \
	usr/share/fonts/reist-jetbrains-mono-12.psf=assets/fonts/reist-jetbrains-mono-12.psf \
	usr/share/fonts/reist-jetbrains-mono-14.psf=assets/fonts/reist-jetbrains-mono-14.psf \
	usr/share/fonts/reist-jetbrains-mono-16.psf=assets/fonts/reist-jetbrains-mono-16.psf \
	usr/share/fonts/reist-jetbrains-mono-18.psf=assets/fonts/reist-jetbrains-mono-18.psf \
	usr/share/fonts/reist-jetbrains-mono-20.psf=assets/fonts/reist-jetbrains-mono-20.psf \
	usr/share/fonts/reist-jetbrains-mono.psf=assets/fonts/reist-jetbrains-mono.psf \
	usr/share/fonts/reist-jetbrains-mono-28.psf=assets/fonts/reist-jetbrains-mono-28.psf \
	usr/share/fonts/reist-source-code-pro-10.psf=assets/fonts/reist-source-code-pro-10.psf \
	usr/share/fonts/reist-source-code-pro-12.psf=assets/fonts/reist-source-code-pro-12.psf \
	usr/share/fonts/reist-source-code-pro-14.psf=assets/fonts/reist-source-code-pro-14.psf \
	usr/share/fonts/reist-source-code-pro-16.psf=assets/fonts/reist-source-code-pro-16.psf \
	usr/share/fonts/reist-source-code-pro-18.psf=assets/fonts/reist-source-code-pro-18.psf \
	usr/share/fonts/reist-source-code-pro-20.psf=assets/fonts/reist-source-code-pro-20.psf \
	usr/share/fonts/reist-source-code-pro.psf=assets/fonts/reist-source-code-pro.psf \
	usr/share/fonts/reist-source-code-pro-28.psf=assets/fonts/reist-source-code-pro-28.psf \
	usr/share/fonts/reist-iosevka-10.psf=assets/fonts/reist-iosevka-10.psf \
	usr/share/fonts/reist-iosevka-12.psf=assets/fonts/reist-iosevka-12.psf \
	usr/share/fonts/reist-iosevka-14.psf=assets/fonts/reist-iosevka-14.psf \
	usr/share/fonts/reist-iosevka-16.psf=assets/fonts/reist-iosevka-16.psf \
	usr/share/fonts/reist-iosevka-18.psf=assets/fonts/reist-iosevka-18.psf \
	usr/share/fonts/reist-iosevka-20.psf=assets/fonts/reist-iosevka-20.psf \
	usr/share/fonts/reist-iosevka.psf=assets/fonts/reist-iosevka.psf \
	usr/share/fonts/reist-iosevka-28.psf=assets/fonts/reist-iosevka-28.psf \
	usr/share/fonts/reist-fira-code-10.psf=assets/fonts/reist-fira-code-10.psf \
	usr/share/fonts/reist-fira-code-12.psf=assets/fonts/reist-fira-code-12.psf \
	usr/share/fonts/reist-fira-code-14.psf=assets/fonts/reist-fira-code-14.psf \
	usr/share/fonts/reist-fira-code-16.psf=assets/fonts/reist-fira-code-16.psf \
	usr/share/fonts/reist-fira-code-18.psf=assets/fonts/reist-fira-code-18.psf \
	usr/share/fonts/reist-fira-code-20.psf=assets/fonts/reist-fira-code-20.psf \
	usr/share/fonts/reist-fira-code.psf=assets/fonts/reist-fira-code.psf \
	usr/share/fonts/reist-fira-code-28.psf=assets/fonts/reist-fira-code-28.psf \
	usr/share/fonts/catalog.toml=assets/fonts/catalog.toml \
	usr/share/fonts/unifont_all-16.0.04.hex.gz=assets/fonts/source/unifont_all-16.0.04.hex.gz \
	usr/share/fonts/ofl-1.1.txt=assets/fonts/source/OFL-1.1.txt \
	usr/share/fonts/jetbrains-mono-ofl.txt=assets/fonts/source/jetbrains-mono/OFL.txt \
	usr/share/fonts/source-code-pro-ofl.txt=assets/fonts/source/source-code-pro/OFL.txt \
	usr/share/fonts/iosevka-ofl.txt=assets/fonts/source/iosevka/OFL.txt \
	usr/share/fonts/fira-code-ofl.txt=assets/fonts/source/fira-code/OFL.txt \
	usr/share/fonts/readme.txt=assets/fonts/README.md \
	usr/share/fonts/unicode.txt=assets/fonts/unicode.txt \
	bin/shell.prg=$(SYSTEM_PROGRAM_DIR)/SHELL.PRG \
	bin/ls.prg=$(SYSTEM_PROGRAM_DIR)/LS.PRG \
	bin/cat.prg=$(SYSTEM_PROGRAM_DIR)/CAT.PRG \
	bin/basic.prg=$(SYSTEM_PROGRAM_DIR)/BASIC.PRG \
	bin/edit.prg=$(SYSTEM_PROGRAM_DIR)/EDIT.PRG \
	bin/pwd.prg=$(SYSTEM_PROGRAM_DIR)/PWD.PRG \
	bin/mkdir.prg=$(SYSTEM_PROGRAM_DIR)/MKDIR.PRG \
	bin/rmdir.prg=$(SYSTEM_PROGRAM_DIR)/RMDIR.PRG \
	bin/del.prg=$(SYSTEM_PROGRAM_DIR)/DEL.PRG \
	bin/copy.prg=$(SYSTEM_PROGRAM_DIR)/COPY.PRG \
	bin/rename.prg=$(SYSTEM_PROGRAM_DIR)/RENAME.PRG \
	bin/ln.prg=$(SYSTEM_PROGRAM_DIR)/LN.PRG \
	bin/readlink.prg=$(SYSTEM_PROGRAM_DIR)/READLINK.PRG \
	bin/stat.prg=$(SYSTEM_PROGRAM_DIR)/STAT.PRG \
	bin/df.prg=$(SYSTEM_PROGRAM_DIR)/DF.PRG \
	bin/touch.prg=$(SYSTEM_PROGRAM_DIR)/TOUCH.PRG \
	bin/tree.prg=$(SYSTEM_PROGRAM_DIR)/TREE.PRG \
	bin/find.prg=$(SYSTEM_PROGRAM_DIR)/FIND.PRG \
	bin/rm.prg=$(SYSTEM_PROGRAM_DIR)/RM.PRG \
	bin/echo.prg=$(SYSTEM_PROGRAM_DIR)/ECHO.PRG \
	bin/cls.prg=$(SYSTEM_PROGRAM_DIR)/CLS.PRG \
	sbin/sysinfo.prg=$(SYSTEM_PROGRAM_DIR)/SYSINFO.PRG \
	sbin/usbinfo.prg=$(SYSTEM_PROGRAM_DIR)/USBINFO.PRG \
	sbin/dmesg.prg=$(SYSTEM_PROGRAM_DIR)/DMESG.PRG \
	sbin/audioinfo.prg=$(SYSTEM_PROGRAM_DIR)/AUDIOINFO.PRG \
	sbin/meminfo.prg=$(SYSTEM_PROGRAM_DIR)/MEMINFO.PRG \
	sbin/chkdsk.prg=$(SYSTEM_PROGRAM_DIR)/CHKDSK.PRG \
	sbin/fdisk.prg=$(SYSTEM_PROGRAM_DIR)/FDISK.PRG \
	sbin/format.prg=$(SYSTEM_PROGRAM_DIR)/FORMAT.PRG \
	sbin/ps.prg=$(SYSTEM_PROGRAM_DIR)/PS.PRG \
	sbin/kill.prg=$(SYSTEM_PROGRAM_DIR)/KILL.PRG \
	sbin/drives.prg=$(SYSTEM_PROGRAM_DIR)/DRIVES.PRG \
	sbin/devctl.prg=$(SYSTEM_PROGRAM_DIR)/DEVCTL.PRG \
	sbin/ifconfig.prg=$(SYSTEM_PROGRAM_DIR)/IFCONFIG.PRG \
	sbin/ping.prg=$(SYSTEM_PROGRAM_DIR)/PING.PRG \
	sbin/netstat.prg=$(SYSTEM_PROGRAM_DIR)/NETSTAT.PRG \
	sbin/udp.prg=$(SYSTEM_PROGRAM_DIR)/UDP.PRG \
	sbin/nslookup.prg=$(SYSTEM_PROGRAM_DIR)/NSLOOKUP.PRG \
	sbin/nc.prg=$(SYSTEM_PROGRAM_DIR)/NC.PRG \
	sbin/httpd.prg=$(SYSTEM_PROGRAM_DIR)/HTTPD.PRG \
	usr/bin/curl.prg=$(SYSTEM_PROGRAM_DIR)/CURL.PRG \
	usr/bin/crtest.prg=$(SYSTEM_PROGRAM_DIR)/CRTEST.PRG \
	usr/bin/mathtest.prg=$(SYSTEM_PROGRAM_DIR)/MATHTEST.PRG \
	usr/bin/texttest.prg=$(SYSTEM_PROGRAM_DIR)/TEXTTEST.PRG \
	usr/bin/memtest.prg=$(SYSTEM_PROGRAM_DIR)/MEMTEST.PRG \
	usr/bin/cpptest.prg=$(SYSTEM_PROGRAM_DIR)/CPPTEST.PRG \
	htdocs/about.txt=htdocs/about.txt \
	htdocs/readme.txt=htdocs/readme.txt \
	htdocs/status.jsn=htdocs/status.jsn \
	htdocs/index.html=htdocs/index.html \
	htdocs/browser-test.html=htdocs/browser-test.html \
	htdocs/browser-html5-test.html=htdocs/browser-html5-test.html \
	htdocs/browser-css-test.html=htdocs/browser-css-test.html \
	htdocs/browser-forms-test.html=htdocs/browser-forms-test.html \
	htdocs/browser-stylesheet-test.html=htdocs/browser-stylesheet-test.html \
	htdocs/browser-stylesheet-main.css=htdocs/browser-stylesheet-main.css \
	htdocs/browser-stylesheet-import.css=htdocs/browser-stylesheet-import.css \
	sbin/mount.prg=$(SYSTEM_PROGRAM_DIR)/MOUNT.PRG \
	sbin/umount.prg=$(SYSTEM_PROGRAM_DIR)/UMOUNT.PRG \
	sbin/svcctl.prg=$(SYSTEM_PROGRAM_DIR)/SVCCTL.PRG \
	sbin/config.prg=$(SYSTEM_PROGRAM_DIR)/CONFIG.PRG \
	usr/bin/repeat.prg=$(SYSTEM_PROGRAM_DIR)/REPEAT.PRG \
	usr/bin/calc.prg=$(SYSTEM_PROGRAM_DIR)/CALC.PRG \
	usr/bin/date.prg=$(SYSTEM_PROGRAM_DIR)/DATE.PRG \
	usr/bin/uptime.prg=$(SYSTEM_PROGRAM_DIR)/UPTIME.PRG \
	usr/bin/benchmark.prg=$(SYSTEM_PROGRAM_DIR)/BENCHMARK.PRG \
	usr/bin/ascii.prg=$(SYSTEM_PROGRAM_DIR)/ASCII.PRG \
	usr/bin/save.prg=$(SYSTEM_PROGRAM_DIR)/SAVE.PRG \
	usr/bin/spawn.prg=$(SYSTEM_PROGRAM_DIR)/SPAWN.PRG \
	usr/bin/audiotest.prg=$(SYSTEM_PROGRAM_DIR)/AUDIOTEST.PRG \
	usr/bin/wavplay.prg=$(SYSTEM_PROGRAM_DIR)/WAVPLAY.PRG \
	usr/gui/bin/desktop.prg=$(SYSTEM_PROGRAM_DIR)/DESKTOP.PRG \
	usr/gui/bin/guidemo.prg=$(SYSTEM_PROGRAM_DIR)/GUIDEMO.PRG \
	usr/gui/bin/notepad.prg=$(SYSTEM_PROGRAM_DIR)/NOTEPAD.PRG \
	usr/gui/bin/browser.prg=$(SYSTEM_PROGRAM_DIR)/BROWSER.PRG \
	usr/bin/htmlwork.prg=$(SYSTEM_PROGRAM_DIR)/HTMLWORK.PRG \
	usr/gui/bin/soundplayer.prg=$(SYSTEM_PROGRAM_DIR)/SOUNDPLAYER.PRG \
	usr/gui/bin/imageviewer.prg=$(SYSTEM_PROGRAM_DIR)/IMAGEVIEWER.PRG \
	usr/gui/bin/control.prg=$(SYSTEM_PROGRAM_DIR)/CONTROL.PRG \
	usr/gui/bin/display.prg=$(SYSTEM_PROGRAM_DIR)/DISPLAY.PRG \
	libexec/reist/childex.prg=$(SYSTEM_PROGRAM_DIR)/CHILDEX.PRG \
	libexec/reist/faultde.prg=$(SYSTEM_PROGRAM_DIR)/FAULTDE.PRG \
	libexec/reist/faultud.prg=$(SYSTEM_PROGRAM_DIR)/FAULTUD.PRG \
	libexec/reist/faultpf.prg=$(SYSTEM_PROGRAM_DIR)/FAULTPF.PRG \
	libexec/reist/faultstk.prg=$(SYSTEM_PROGRAM_DIR)/FAULTSTK.PRG \
	libexec/reist/gtest.prg=$(SYSTEM_PROGRAM_DIR)/GTEST.PRG \
	libexec/reist/reist.prg=$(SYSTEM_PROGRAM_DIR)/REIST.PRG \
	libexec/reist/storage.prg=$(SYSTEM_PROGRAM_DIR)/STORAGE.PRG \
	libexec/reist/hda.prg=$(SYSTEM_PROGRAM_DIR)/HDA.PRG \
	libexec/reist/svga2d.prg=$(SYSTEM_PROGRAM_DIR)/SVGA2D.PRG \
	libexec/reist/nvidia.prg=$(SYSTEM_PROGRAM_DIR)/NVIDIA.PRG \
	libexec/reist/audio.prg=$(SYSTEM_PROGRAM_DIR)/AUDIO.PRG \
	libexec/reist/sleeper.prg=$(SYSTEM_PROGRAM_DIR)/SLEEPER.PRG \
	libexec/reist/capwait.prg=$(SYSTEM_PROGRAM_DIR)/CAPWAIT.PRG \
	libexec/reist/satawr.prg=$(SYSTEM_PROGRAM_DIR)/SATAWR.PRG

# The rescue floppy intentionally omits optional audio components.  The full
# HDD/VMware image remains authoritative; a 1.44-MiB recovery medium must keep
# enough free space for filesystem metadata and emergency tools.
FLOPPY_IMAGE_FILES := $(filter-out \
	sbin/audioinfo.prg=% usr/bin/audiotest.prg=% usr/bin/wavplay.prg=% \
	usr/gui/bin/soundplayer.prg=% usr/gui/bin/imageviewer.prg=% \
	usr/gui/bin/browser.prg=% usr/bin/htmlwork.prg=% htdocs/index.html=% htdocs/browser-test.html=% htdocs/browser-html5-test.html=% htdocs/browser-css-test.html=% htdocs/browser-forms-test.html=% \
	htdocs/browser-stylesheet-test.html=% htdocs/browser-stylesheet-main.css=% htdocs/browser-stylesheet-import.css=% \
	usr/share/sounds/startup.wav=% \
	usr/share/sounds/shutdown.wav=% \
	usr/share/sounds/error.wav=% usr/share/sounds/notify.wav=% \
	usr/share/sounds/trash-drop.wav=% usr/share/sounds/trash-empty.wav=% \
	usr/share/sounds/trash-drop.wav=% usr/share/sounds/trash-empty.wav=% \
	usr/share/images/demo-desktop.bmp=% \
	usr/share/images/reist-splash.bmp=% \
	usr/share/images/demo-colors.gif=% \
	usr/share/fonts/reist-unicode.psf=% \
	usr/share/fonts/reist-jetbrains-mono% \
	usr/share/fonts/reist-source-code-pro% \
	usr/share/fonts/reist-iosevka% usr/share/fonts/reist-fira-code% \
	usr/share/fonts/catalog.toml=% usr/share/fonts/jetbrains-mono-ofl.txt=% \
	usr/share/fonts/source-code-pro-ofl.txt=% \
	usr/share/fonts/iosevka-ofl.txt=% usr/share/fonts/fira-code-ofl.txt=% \
	usr/share/fonts/unifont_all-16.0.04.hex.gz=% \
	usr/share/fonts/ofl-1.1.txt=% usr/share/fonts/readme.txt=% \
	usr/share/fonts/unicode.txt=% \
	usr/share/icons/folder-empty.ico=% usr/share/icons/folder-full.ico=% \
	usr/share/icons/program.ico=% usr/share/icons/text.ico=% \
	usr/share/icons/audio.ico=% usr/share/icons/image.ico=% \
	usr/share/icons/settings.ico=% usr/share/icons/unknown.ico=% \
	usr/share/icons/trash-empty.ico=% usr/share/icons/trash-full.ico=% \
	usr/share/icons/shortcut.ico=% \
	libexec/reist/hda.prg=% libexec/reist/audio.prg=% \
	libexec/reist/svga2d.prg=% libexec/reist/nvidia.prg=%,$(SYSTEM_IMAGE_FILES))

bootdisk: native-image

user-sdk: check-syscall-abi
	@$(PYTHON) scripts/build_user_sdk.py --output-dir $(USER_SDK_DIR) --zig $(ZIG) --incremental

user-program: user-sdk
	@echo "Building external user program $(USER_PROGRAM_SOURCE)..."
	@$(PYTHON) -c "from pathlib import Path; Path('$(USER_PROGRAM_OUTPUT)').parent.mkdir(parents=True, exist_ok=True)"
	@$(PYTHON) scripts/build_user_program.py $(USER_PROGRAM_SOURCE) \
		--output $(USER_PROGRAM_OUTPUT) --zig $(ZIG) \
		--sysroot $(USER_SDK_DIR) --incremental $(USER_PROGRAM_LIBS)

system-programs: check-syscall-abi
	@echo "Building standard Ring-3 system programs..."
	@$(PYTHON) scripts/build_system_programs.py \
		--output-dir $(SYSTEM_PROGRAM_DIR) --zig $(ZIG) --incremental

signed-kernel: kernel
	@echo "Signing and verifying the research kernel artifact..."
	@$(PYTHON) scripts/sign_boot_artifact.py \
		--artifact $(OUTPUT_DIR)/kernel.bin --signature $(BOOT_SIGNATURE) \
		--private-key $(BOOT_PRIVATE_KEY) --policy $(BOOT_TRUST_POLICY) \
		--openssl $(OPENSSL) --profile research
	@$(PYTHON) scripts/verify_boot_signature.py \
		--artifact $(OUTPUT_DIR)/kernel.bin --signature $(BOOT_SIGNATURE) \
		--policy $(BOOT_TRUST_POLICY) --openssl $(OPENSSL) --root .

native-image: floppy-image
	@echo "Creating native BIOS disk image..."
	@$(AS) -f bin arch/$(ARCH)/boot/bios/stage1_mbr.asm -o $(OUTPUT_DIR)/stage1_mbr.bin
	@$(AS) $(VIDEO_DEFINES) -f bin arch/$(ARCH)/boot/bios/stage2_bios.asm -o $(OUTPUT_DIR)/stage2_bios.bin
	@$(PYTHON) scripts/create_native_boot_image.py \
		--stage1 $(OUTPUT_DIR)/stage1_mbr.bin \
		--stage2 $(OUTPUT_DIR)/stage2_bios.bin \
		--kernel $(OUTPUT_DIR)/kernel.bin \
		--signature $(BOOT_SIGNATURE) \
		--output $(OUTPUT_DIR)/reist-os.img \
		--vmdk $(OUTPUT_DIR)/reist-os.vmdk \
		--vmware-dir $(OUTPUT_DIR)/vmware/reist-os \
		--floppy $(OUTPUT_DIR)/reist-os-floppy.img \
		--data-file usr/bin/hello.prg=$(USER_PROGRAM_OUTPUT) \
		$(foreach spec,$(SYSTEM_IMAGE_FILES),--data-file $(spec))
	@$(PYTHON) scripts/validate_boot_manifest.py \
		--image $(OUTPUT_DIR)/reist-os.img --layout hdd
	@$(PYTHON) scripts/generate_release_sbom.py --root . \
		--output $(OUTPUT_DIR)/reist-sbom.spdx.json \
		--artifact $(OUTPUT_DIR)/kernel.bin --artifact $(BOOT_SIGNATURE) \
		--artifact $(OUTPUT_DIR)/reist-os.img --program-dir $(SYSTEM_PROGRAM_DIR)
	@$(PYTHON) scripts/validate_release_sbom.py \
		--sbom $(OUTPUT_DIR)/reist-sbom.spdx.json --root .
	@echo "Native BIOS image created: $(OUTPUT_DIR)/reist-os.img"
	@echo "Complete VMware VM: $(OUTPUT_DIR)/vmware/reist-os/reist-os.vmx"

floppy-image: signed-kernel system-programs user-program
	@echo "Creating 1.44-MB BIOS floppy image..."
	@$(AS) -f bin arch/$(ARCH)/boot/bios/stage1_floppy.asm -o $(OUTPUT_DIR)/stage1_floppy.bin
	@$(AS) $(VIDEO_DEFINES) -f bin arch/$(ARCH)/boot/bios/stage2_bios.asm -o $(OUTPUT_DIR)/stage2_bios.bin
	@$(PYTHON) scripts/create_floppy_boot_image.py \
		--stage1 $(OUTPUT_DIR)/stage1_floppy.bin \
		--stage2 $(OUTPUT_DIR)/stage2_bios.bin \
		--kernel $(OUTPUT_DIR)/kernel.bin \
		--signature $(BOOT_SIGNATURE) \
		--output $(OUTPUT_DIR)/reist-os-floppy.img \
		--data-file usr/bin/hello.prg=$(USER_PROGRAM_OUTPUT) \
		$(foreach spec,$(FLOPPY_IMAGE_FILES),--data-file $(spec))
	@$(PYTHON) scripts/validate_boot_manifest.py \
		--image $(OUTPUT_DIR)/reist-os-floppy.img --layout floppy

# ============================================================================
# TESTING
# ============================================================================

# Complete fixture-independent host suite for CI and milestone validation.
test: test-unit

test-unit:
	@echo "Running host-side unit tests..."
	@$(PYTHON) -m unittest discover -s test -p "test_*.py" -v

# Bounded inner loop for desktop/menu implementation work.
test-desktop-host:
	@$(PYTHON) test/test_display_abi_minimal.py -q
	@$(PYTHON) test/test_gui_font.py -q
	@$(PYTHON) test/test_gui_bmp_font.py -q
	@$(PYTHON) test/test_desktop_source.py -q
	@$(PYTHON) test/test_desktop_explorer_source.py -q
	@$(PYTHON) test/test_desktop_filetypes_source.py -q
	@$(PYTHON) test/test_gui_menu_source.py -q
	@$(PYTHON) test/test_gui_dialog_source.py -q
	@$(PYTHON) test/test_gui_file_dialog_source.py -q
	@$(PYTHON) test/test_gui_control_source.py -q
	@$(PYTHON) test/test_gui_container_source.py -q
	@$(PYTHON) test/test_gui_tabs_source.py -q
	@$(PYTHON) test/test_gui_value_controls_source.py -q
	@$(PYTHON) test/test_gui_text_editor_source.py -q
	@$(PYTHON) test/test_gui_control_gallery_source.py -q
	@$(PYTHON) test/test_gui_notepad_source.py -q

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

test-smoke-fdd-hotplug: native-image
	@echo "Running live FDD disconnect/reconnect recovery test..."
	@$(PYTHON) scripts/run_qemu_fdd_hotplug.py \
		--qemu $(QEMU) \
		--image $(OUTPUT_DIR)/reist-os.img \
		--floppy $(OUTPUT_DIR)/fdd-hotplug.img \
		--log $(OUTPUT_DIR)/guest-smoke-fdd-hotplug.log

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

test-smoke-runtime-degradation:
	@echo "Building deterministic runtime-degradation guard image..."
	@$(MAKE) native-image TARGET=qemu VIDEO=vga \
		RUNTIME_DEGRADATION_FAULT_INJECTION=1 \
		OUTPUT_DIR=build/runtime-degradation
	@$(PYTHON) scripts/run_qemu_smoke.py \
		--qemu $(QEMU) \
		--image build/runtime-degradation/reist-os.img \
		--expect-runtime-degradation \
		--timeout 120 \
		--log build/runtime-degradation/guest-smoke.log

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
QEMU_COMMON := -m 1024M -cpu qemu32,+rdrand $(QEMU_IMAGE) -no-reboot -no-shutdown

run: run-native
run-disk: run-native

run-native: native-image
	@echo "Starting QEMU through the native BIOS/MBR path..."
	@$(QEMU) $(QEMU_COMMON) \
		-device rtl8139,netdev=net0 -netdev user,id=net0 \
		-vga std

run-usb: native-image
	@echo "Starting QEMU with the xHCI USB keyboard profile..."
	@$(QEMU) $(QEMU_COMMON) \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,bus=xhci.0 \
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
	@$(QEMU) -m 1024M -boot a \
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
