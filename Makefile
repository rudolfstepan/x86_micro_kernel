# Variables
SOURCE_DIR = src
OUTPUT_DIR = build
ISO_DIR = iso
CFLAGS = -Isrc -m32 -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -O1 -Wall -Wextra -g -Wno-unused-parameter -Wno-unused-variable -U_FORTIFY_SOURCE
LD_FLAGS = -m elf_i386 -nostdlib
MOUNT_DIR = /mnt/disk

# -m32 -c -ffreestanding -fno-asynchronous-unwind-tables -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector -O0 -Wall -Wextra -g -Wno-unused-parameter

# Directories
BOOT_DIR = $(OUTPUT_DIR)/boot
FILESYSTEM_DIR = $(OUTPUT_DIR)/filesystem
DRIVERS_DIR = $(OUTPUT_DIR)/drivers
CLI_DIR = $(OUTPUT_DIR)/cli

.PHONY: all clean prepare compile_bootloader compile_filesystem compile_drivers compile_sources link_kernel link_cli copy_binaries mount iso run

all: clean prepare compile_bootloader compile_filesystem compile_drivers compile_sources link_kernel link_cli copy_binaries mount iso

clean:
	clear
	@echo "Cleaning up build and iso directories..."
	rm -rf $(OUTPUT_DIR)/*
	rm -rf $(ISO_DIR)/*

prepare:
	@echo "Preparing directories..."
	mkdir -p $(BOOT_DIR) $(FILESYSTEM_DIR) $(DRIVERS_DIR) $(CLI_DIR)

# Compile bootloader
compile_bootloader:
	@echo "Compiling bootloader..."
	nasm -f elf32 $(SOURCE_DIR)/boot/bootloader.asm -o $(BOOT_DIR)/_bootloader.o
	nasm -f elf32 $(SOURCE_DIR)/boot/multiboot.asm -o $(BOOT_DIR)/_multiboot.o
	nasm -f elf32 $(SOURCE_DIR)/boot/gdt.asm -o $(BOOT_DIR)/_gdt.o
	nasm -f elf32 $(SOURCE_DIR)/boot/idt.asm -o $(BOOT_DIR)/_idt.o
	nasm -f elf32 $(SOURCE_DIR)/boot/isr.asm -o $(BOOT_DIR)/_isr.o
	nasm -f elf32 $(SOURCE_DIR)/boot/irq.asm -o $(BOOT_DIR)/_irq.o
	nasm -f elf32 $(SOURCE_DIR)/boot/syscall.asm -o $(BOOT_DIR)/_syscall.o
	nasm -f elf32 $(SOURCE_DIR)/boot/stack.asm -o $(BOOT_DIR)/_stack.o
	

# Compile filesystem sources
compile_filesystem:
	@echo "Compiling filesystem sources..."
	for src_file in $(shell find $(SOURCE_DIR)/filesystem -type f -name '*.c'); do \
		base_name=$$(basename $$src_file .c); \
		gcc $(CFLAGS) -c $$src_file -o $(FILESYSTEM_DIR)/_$${base_name}.o; \
	done
	@echo "Linking filesystem object files..."
	ld -m elf_i386 -r -o $(FILESYSTEM_DIR)/filesystem.o $(FILESYSTEM_DIR)/_*.o

# Compile drivers sources
compile_drivers:
	@echo "Compiling drivers sources..."
	for src_file in $(shell find $(SOURCE_DIR)/drivers -type f -name '*.c'); do \
		base_name=$$(basename $$src_file .c); \
		gcc $(CFLAGS) -c $$src_file -o $(DRIVERS_DIR)/_$${base_name}.o; \
	done
	@echo "Linking drivers object files..."
	ld -m elf_i386 -r -o $(DRIVERS_DIR)/drivers.o $(DRIVERS_DIR)/_*.o

# Compile all .c files except in filesystem and drivers directories
compile_sources:
	@echo "Compiling sources..."
	for FILE in $(shell find $(SOURCE_DIR) -type f -name '*.c' ! -path '$(SOURCE_DIR)/filesystem/*' ! -path '$(SOURCE_DIR)/drivers/*'); do \
		RELATIVE_PATH=$${FILE#$(SOURCE_DIR)/}; \
		mkdir -p $(OUTPUT_DIR)/$$(dirname $$RELATIVE_PATH); \
		OBJECT_NAME=$${RELATIVE_PATH%.*}.o; \
		gcc $(CFLAGS) -o $(OUTPUT_DIR)/$$OBJECT_NAME $$FILE; \
	done

	nasm -f elf32 $(SOURCE_DIR)/kernel/switch.asm -o $(OUTPUT_DIR)/kernel/switch.o

# Link kernel
link_kernel:
	@echo "Linking kernel..."
	ld $(LD_FLAGS) -T klink.ld -o $(OUTPUT_DIR)/kernel.bin $(OUTPUT_DIR)/kernel/kernel.o \
	$(OUTPUT_DIR)/kernel/switch.o $(OUTPUT_DIR)/kernel/scheduler.o $(OUTPUT_DIR)/kernel/memory.o \
	$(BOOT_DIR)/_multiboot.o $(BOOT_DIR)/_bootloader.o $(BOOT_DIR)/_gdt.o $(BOOT_DIR)/_idt.o $(BOOT_DIR)/_isr.o \
	$(BOOT_DIR)/_irq.o $(BOOT_DIR)/_syscall.o $(BOOT_DIR)/_stack.o $(OUTPUT_DIR)/kernel/gdt.o \
	$(OUTPUT_DIR)/kernel/idt.o $(OUTPUT_DIR)/kernel/isr.o $(OUTPUT_DIR)/kernel/irq.o $(OUTPUT_DIR)/kernel/pit.o \
	$(OUTPUT_DIR)/kernel/hpet.o $(OUTPUT_DIR)/kernel/apic.o $(OUTPUT_DIR)/kernel/prg.o $(OUTPUT_DIR)/kernel/command.o \
	$(OUTPUT_DIR)/kernel/process.o $(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o \
	$(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/string.o
	
# Link CLI programs
link_cli:

	# # @echo "Linking cli_date..."
	# ld $(LD_FLAGS) -T cli.ld -o $(CLI_DIR)/date.elf $(CLI_DIR)/date.o \
	# $(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/string.o $(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o

	# # @echo "Linking cli_dir..."
	# ld $(LD_FLAGS) -T cli.ld -o $(CLI_DIR)/dir.elf $(CLI_DIR)/dir.o \
	# $(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/string.o $(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o

	# @echo "Linking cli_test..."
	# ld $(LD_FLAGS) -T cli.ld -o $(CLI_DIR)/test.elf $(CLI_DIR)/test.o \
	# $(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/string.o $(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o

# Copy binaries
copy_binaries:
	@echo "Copying binaries..."
	# objcopy -O binary $(CLI_DIR)/date.elf $(CLI_DIR)/date.prg
	# objcopy -O binary $(CLI_DIR)/dir.elf $(CLI_DIR)/dir.prg
	# objcopy -O binary $(CLI_DIR)/test.elf $(CLI_DIR)/test.prg

mount:
	@echo "Mounting disk image..."
	# if [ ! -d $(MOUNT_DIR) ]; then \
	# 	echo "Creating $(MOUNT_DIR) directory"; \
	# 	mkdir -p $(MOUNT_DIR); \
	# fi
	# sudo mount ./disk.img $(MOUNT_DIR)
	# sudo cp $(CLI_DIR)/date.prg $(MOUNT_DIR)/
	# sudo cp $(CLI_DIR)/dir.prg $(MOUNT_DIR)/
	# sudo cp $(CLI_DIR)/test.prg $(MOUNT_DIR)/
	# sudo umount $(MOUNT_DIR)

	# @echo "Converting disk image to VMDK format..."
	# qemu-img convert -f raw -O vmdk ./disk.img ./disk.vmdk

iso:
	@echo "Creating ISO image..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(OUTPUT_DIR)/kernel.bin $(ISO_DIR)/boot/
	cp grub.cfg $(ISO_DIR)/boot/grub/
	sudo grub-mkrescue -o kernel.iso $(ISO_DIR)

run:
	@echo "Running QEMU..."
	qemu-system-x86_64 -m 512M -boot d -cdrom ./kernel.iso -drive file=./disk.img,format=raw -drive file=./floppy.img,format=raw,if=floppy \
	-device ne2k_pci,netdev=net0 -netdev user,id=net0 -monitor stdio -vga vmware
	
	#-device rtl8139,netdev=net1 -netdev user,id=net1 -object filter-dump,id=f1,netdev=net1,file=dump.dat
	#-device e1000,netdev=net0 -netdev user,id=net0 -monitor stdio \
	#-netdev user,id=mynet0 -device e1000,netdev=mynet0 \
	#-device rtl8139,netdev=mynet1 -netdev user,id=mynet1 -monitor stdio \
	#-object filter-dump,id=f1,netdev=mynet0,file=dump.dat
	#qemu-system-x86_64 -bios OVMF.fd -kernel ./build/kernel.bin
	#qemu-system-x86_64 -kernel ./build/kernel.bin -s -S -display default
	