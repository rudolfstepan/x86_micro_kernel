# Variables
SOURCE_DIR = src
OUTPUT_DIR = build
ISO_DIR = iso
CXXFLAGS = -Isrc -m32 -nostartfiles -nodefaultlibs -fno-builtin -O1 -Wall -Wextra -g \
           -Wno-unused-parameter -Wno-unused-variable -U_FORTIFY_SOURCE -fno-exceptions -U_FORTIFY_SOURCE -fno-stack-protector
LD_FLAGS = -m elf_i386 -g -L/usr/lib/gcc/x86_64-linux-gnu/11/32/libgcc.a
MOUNT_DIR = /mnt/disk

CC = i686-linux-gnu-gcc
LD = i686-linux-gnu-ld
ASM = nasm


.PHONY: all clean prepare compile_asm compile_sources link_kernel iso run

all: clean prepare compile_asm compile_sources link_kernel iso

clean:
	@echo "Cleaning up build and iso directories..."
	rm -rf $(OUTPUT_DIR)/*
	rm -rf $(ISO_DIR)/*

prepare:
	@echo "Preparing directories..."
	mkdir -p $(OUTPUT_DIR)

compile_asm:
	@echo "Compiling assembly files..."
	for asm_file in $(shell find $(SOURCE_DIR) -type f -name '*.asm'); do \
		dir_name=$$(basename $$(dirname $$asm_file)); \
		file_name=$$(basename $$asm_file .asm); \
		output_file=$${dir_name}_$${file_name}.o; \
		echo "Compiling $$asm_file to $(OUTPUT_DIR)/$$output_file"; \
		$(ASM) -f elf32 $$asm_file -o $(OUTPUT_DIR)/$$output_file || exit 1; \
	done

compile_sources:
	@echo "Compiling sources..."
	for src_file in $(shell find $(SOURCE_DIR) -type f -name '*.c' ! -path '$(SOURCE_DIR)/drivers/network/*' ! -path '$(SOURCE_DIR)/cli/*'); do \
		dir_name=$$(basename $$(dirname $$src_file)); \
		file_name=$$(basename $$src_file .c); \
		output_file=$${dir_name}_$${file_name}.o; \
		echo "Compiling $$src_file to $(OUTPUT_DIR)/$$output_file"; \
		$(CC) $(CXXFLAGS) -c $$src_file -o $(OUTPUT_DIR)/$$output_file || exit 1; \
	done

# Link kernel
link_kernel:
	@echo "Linking kernel..."
	$(LD) $(LD_FLAGS) -T klink.ld -o ./kernel.bin $(shell find $(OUTPUT_DIR) -type f -name '*.o')

iso:
	@echo "Creating ISO image..."
	mkdir -p $(ISO_DIR)/boot/grub
	cp kernel.bin $(ISO_DIR)/boot/
	cp grub.cfg $(ISO_DIR)/boot/grub/
	sudo grub-mkrescue -o kernel.iso $(ISO_DIR)

run:
	@echo "Running QEMU..."
	qemu-system-x86_64 -cdrom kernel.iso -boot d -m 512M -vga std -drive file=disk.img,format=raw -drive file=floppy.img,format=raw,if=floppy \
	#-device ne2k_pci,netdev=net0 -netdev user,id=net0
	