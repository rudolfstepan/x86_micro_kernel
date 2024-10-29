#!/bin/bash

# Define the source directory
SOURCE_DIR=src
OUTPUT_DIR=build
ISO_DIR=iso
CFLAGS="-Isrc -m32 -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -g"

echo "Cleaning up build directory..."
sudo rm -rf $OUTPUT_DIR/*

echo "Cleaning up iso directory..."
sudo rm -rf $ISO_DIR/*

# Create output directory if it doesn't exist
mkdir -p $OUTPUT_DIR

# Compile to object file

# bootloader
echo "Compiling bootloader..."
# Ensure the output directory and its subdirectories exist
mkdir -p "$OUTPUT_DIR/boot"
nasm -f elf32 $SOURCE_DIR/boot/bootloader.asm -o $OUTPUT_DIR/boot/bootloader.o
echo "done"

# Compile FAT32 sources separately because they need to be linked into a single object file
echo "Compiling FAT32 sources..."
mkdir -p "$OUTPUT_DIR/filesystem"
gcc $CFLAGS -o "$OUTPUT_DIR/filesystem/fat32_main.o" "$SOURCE_DIR/filesystem/fat32/fat32.c"
gcc $CFLAGS -o "$OUTPUT_DIR/filesystem/fat32_cluster.o" "$SOURCE_DIR/filesystem/fat32/fat32_cluster.c"
gcc $CFLAGS -o "$OUTPUT_DIR/filesystem/fat32_files.o" "$SOURCE_DIR/filesystem/fat32/fat32_files.c"
gcc $CFLAGS -o "$OUTPUT_DIR/filesystem/fat32_dir.o" "$SOURCE_DIR/filesystem/fat32/fat32_dir.c"

# Link into a single fat32.o
echo "Linking FAT32 object files..."
ld -m elf_i386 -r -o "$OUTPUT_DIR/filesystem/fat32.o" \
    "$OUTPUT_DIR/filesystem/fat32_main.o" \
    "$OUTPUT_DIR/filesystem/fat32_cluster.o" \
    "$OUTPUT_DIR/filesystem/fat32_files.o" \
    "$OUTPUT_DIR/filesystem/fat32_dir.o"
echo "done"


# # compile all .c files
# echo "Compiling sources..."
# for FILE in $(find $SOURCE_DIR -type f -name "*.c"); do
#     # Get the directory structure of the source file relative to $SOURCE_DIR
#     RELATIVE_PATH=${FILE#$SOURCE_DIR/}
#     # Create corresponding directory structure in the output directory
#     mkdir -p "$OUTPUT_DIR/$(dirname $RELATIVE_PATH)"
    
#     # Extract the base filename and compile it to the corresponding output path
#     OBJECT_NAME="${RELATIVE_PATH%.*}.o"
#     gcc $CFLAGS -o "$OUTPUT_DIR/$OBJECT_NAME" "$FILE"
# done
# echo "done"

# Compile all .c files except in the filesystem directory
echo "Compiling sources..."
for FILE in $(find $SOURCE_DIR -type f -name "*.c"); do
    # Skip files in the filesystem directory
    if [[ "$FILE" == *"/filesystem/"* ]]; then
        continue
    fi
    
    # Get the directory structure of the source file relative to $SOURCE_DIR
    RELATIVE_PATH=${FILE#$SOURCE_DIR/}
    # Create corresponding directory structure in the output directory
    mkdir -p "$OUTPUT_DIR/$(dirname $RELATIVE_PATH)"
    
    # Extract the base filename and compile it to the corresponding output path
    OBJECT_NAME="${RELATIVE_PATH%.*}.o"
    gcc $CFLAGS -o "$OUTPUT_DIR/$OBJECT_NAME" "$FILE"
done
echo "done"

#Link using the linker script
echo "Linking kernel..."
ld -m elf_i386 -T kernel.ld -nostdlib -o $OUTPUT_DIR/kernel.bin \
    $OUTPUT_DIR/boot/bootloader.o $OUTPUT_DIR/boot/gdt.o $OUTPUT_DIR/boot/idt.o $OUTPUT_DIR/boot/isr.o \
    $OUTPUT_DIR/drivers/io/io.o $OUTPUT_DIR/kernel/irq.o $OUTPUT_DIR/kernel/kernel.o $OUTPUT_DIR/kernel/prg.o $OUTPUT_DIR/kernel/system.o \
    $OUTPUT_DIR/drivers/video/video.o $OUTPUT_DIR/drivers/ata/ata.o $OUTPUT_DIR/drivers/keyboard/keyboard.o $OUTPUT_DIR/drivers/pit/pit.o $OUTPUT_DIR/drivers/rtc/rtc.o \
    $OUTPUT_DIR/filesystem/fat32.o \
    $OUTPUT_DIR/toolchain/stdlib.o $OUTPUT_DIR/toolchain/stdio.o $OUTPUT_DIR/toolchain/strings.o \

echo "Linking cli_date..."
ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli/cli_date.elf $OUTPUT_DIR/cli/cli_date.o \
    $OUTPUT_DIR/drivers/rtc/rtc.o $OUTPUT_DIR/drivers/io/io.o $OUTPUT_DIR/drivers/video/video.o $OUTPUT_DIR/drivers/ata/ata.o \
    $OUTPUT_DIR/toolchain/stdlib.o $OUTPUT_DIR/toolchain/stdio.o $OUTPUT_DIR/toolchain/strings.o \
    $OUTPUT_DIR/filesystem/fat32.o \
    
    
# ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli/cli_dir.elf $OUTPUT_DIR/cli/cli_dir.o \
#     $OUTPUT_DIR/drivers/video/video.o $OUTPUT_DIR/drivers/ata/ata.o $OUTPUT_DIR/drivers/keyboard/keyboard.o $OUTPUT_DIR/drivers/pit/pit.o $OUTPUT_DIR/drivers/rtc/rtc.o \
#     $OUTPUT_DIR/filesystem/fat32.o $OUTPUT_DIR/filesystem/fat32_cluster.o $OUTPUT_DIR/filesystem/fat32_dir.o $OUTPUT_DIR/filesystem/fat32_file.o \
#     $OUTPUT_DIR/toolchain/stdlib.o $OUTPUT_DIR/toolchain/stdio.o $OUTPUT_DIR/toolchain/strings.o \

echo "Linking cli_test..."
ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli/cli_test.elf $OUTPUT_DIR/cli/cli_test.o \
    $OUTPUT_DIR/drivers/rtc/rtc.o $OUTPUT_DIR/drivers/io/io.o $OUTPUT_DIR/drivers/video/video.o $OUTPUT_DIR/drivers/ata/ata.o \
    $OUTPUT_DIR/toolchain/stdlib.o $OUTPUT_DIR/toolchain/stdio.o $OUTPUT_DIR/toolchain/strings.o \
    $OUTPUT_DIR/filesystem/fat32.o \

# ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/basic.elf $OUTPUT_DIR/basic.o \
#     $OUTPUT_DIR/drivers/video/video.o $OUTPUT_DIR/drivers/ata/ata.o $OUTPUT_DIR/drivers/keyboard/keyboard.o $OUTPUT_DIR/drivers/pit/pit.o $OUTPUT_DIR/drivers/rtc/rtc.o \
#     $OUTPUT_DIR/filesystem/fat32.o $OUTPUT_DIR/filesystem/fat32_cluster.o $OUTPUT_DIR/filesystem/fat32_dir.o $OUTPUT_DIR/filesystem/fat32_file.o \
#     $OUTPUT_DIR/toolchain/stdlib.o $OUTPUT_DIR/toolchain/stdio.o $OUTPUT_DIR/toolchain/strings.o \


# Convert to binary format
objcopy -O binary $OUTPUT_DIR/cli/cli_date.elf $OUTPUT_DIR/cli/date.prg
#objcopy -O binary $OUTPUT_DIR/cli/cli_dir.elf $OUTPUT_DIR/cli/dir.prg
objcopy -O binary $OUTPUT_DIR/cli/cli_test.elf $OUTPUT_DIR/cli/test.prg
# # objcopy -O binary $OUTPUT_DIR/basic.elf $OUTPUT_DIR/basic.prg

#./make_image.sh

# Check if directory exists, create it if not
  if [ ! -d /mnt/disk ]; then
    echo "Creating /mnt/disk directory"
    mkdir -p /mnt/disk
  fi
sudo mount ./disk.img /mnt/disk
sudo cp $OUTPUT_DIR/cli/date.prg /mnt/disk/sys
# sudo cp $OUTPUT_DIR/dir.prg /mnt/disk/sys
sudo cp $OUTPUT_DIR/cli/test.prg /mnt/disk/sys
# sudo cp $OUTPUT_DIR/basic.prg /mnt/disk/
sudo umount /mnt/disk

# Create the iso file for kernel
mkdir -p $ISO_DIR/boot/grub
cp $OUTPUT_DIR/kernel.bin $ISO_DIR/boot/
cp grub.cfg $ISO_DIR/boot/grub/
sudo grub-mkrescue -o kernel.iso $ISO_DIR/

# ./run.sh