# Variables
SOURCE_DIR = src
OUTPUT_DIR = build
ISO_DIR = iso
CFLAGS = -Isrc -m32 -c -ffreestanding -fno-asynchronous-unwind-tables -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -g -Wno-unused-parameter
LD_FLAGS = -m elf_i386 -nostdlib
MOUNT_DIR = /mnt/disk

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
	nasm -f elf32 $(SOURCE_DIR)/boot/boot2.asm -o $(BOOT_DIR)/bootloader.o

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

# Link kernel
link_kernel:
	@echo "Linking kernel..."
	ld $(LD_FLAGS) -T klink.ld -o $(OUTPUT_DIR)/kernel.bin $(OUTPUT_DIR)/kernel/process.o \
	$(BOOT_DIR)/bootloader.o \
	$(OUTPUT_DIR)/boot/gdt.o $(OUTPUT_DIR)/boot/idt.o $(OUTPUT_DIR)/boot/isr.o \
	$(OUTPUT_DIR)/kernel/irq.o $(OUTPUT_DIR)/kernel/pit.o $(OUTPUT_DIR)/kernel/kernel.o $(OUTPUT_DIR)/kernel/prg.o $(OUTPUT_DIR)/kernel/system.o $(OUTPUT_DIR)/kernel/command.o \
	$(DRIVERS_DIR)/drivers.o \
	$(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/strings.o \
	$(FILESYSTEM_DIR)/filesystem.o

# Link CLI programs
link_cli:
	@echo "Linking cli_date..."
	ld $(LD_FLAGS) -T linkprg.ld -o $(CLI_DIR)/cli_date.elf $(CLI_DIR)/cli_date.o \
	$(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o \
	$(OUTPUT_DIR)/kernel/pit.o \
	$(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/strings.o
	@echo "Linking cli_test..."
	ld $(LD_FLAGS) -T linkprg.ld -o $(CLI_DIR)/cli_test.elf $(CLI_DIR)/cli_test.o \
	$(OUTPUT_DIR)/kernel/pit.o \
	$(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o \
	$(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/strings.o
	@echo "Linking cli_dir..."
	ld $(LD_FLAGS) -T linkprg.ld -o $(CLI_DIR)/cli_dir.elf $(CLI_DIR)/cli_dir.o \
	$(OUTPUT_DIR)/kernel/pit.o \
	$(DRIVERS_DIR)/drivers.o $(FILESYSTEM_DIR)/filesystem.o \
	$(OUTPUT_DIR)/toolchain/stdlib.o $(OUTPUT_DIR)/toolchain/stdio.o $(OUTPUT_DIR)/toolchain/strings.o

# Copy binaries
copy_binaries:
	@echo "Copying binaries..."
	objcopy -O binary $(CLI_DIR)/cli_date.elf $(CLI_DIR)/date.prg
	objcopy -O binary $(CLI_DIR)/cli_test.elf $(CLI_DIR)/test.prg
	objcopy -O binary $(CLI_DIR)/cli_dir.elf $(CLI_DIR)/dir.prg

mount:
	@echo "Mounting disk image..."
	if [ ! -d $(MOUNT_DIR) ]; then \
		echo "Creating $(MOUNT_DIR) directory"; \
		mkdir -p $(MOUNT_DIR); \
	fi
	sudo mount ./disk.img $(MOUNT_DIR)
	sudo cp $(CLI_DIR)/date.prg $(MOUNT_DIR)/
	sudo cp $(CLI_DIR)/test.prg $(MOUNT_DIR)/
	sudo cp $(CLI_DIR)/dir.prg $(MOUNT_DIR)/
	sudo umount $(MOUNT_DIR)

iso:
	@echo "Creating ISO image..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(OUTPUT_DIR)/kernel.bin $(ISO_DIR)/boot/
	cp grub.cfg $(ISO_DIR)/boot/grub/
	sudo grub-mkrescue -o kernel.iso $(ISO_DIR)

run:
	@echo "Running QEMU..."
	qemu-system-x86_64 -boot d -cdrom ./kernel.iso -audiodev id=audio0,driver=pa -machine pcspk-audiodev=audio0 -drive file=./disk.img,format=raw -drive file=./disk1.img,format=raw -drive file=./floppy.img,format=raw,if=floppy -drive file=./fdd.img,format=raw,if=floppy \