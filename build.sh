#!/bin/bash

SOURCE_DIR=src
OUTPUT_DIR=build
ISO_DIR=iso
CFLAGS="-m32 -g -c -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -g"

echo "Cleaning up build directory..."
rm -rf $OUTPUT_DIR/*

# echo "Cleaning up iso directory..."
# rm -rf $ISO_DIR/*

# Create output directory if it doesn't exist
mkdir -p $OUTPUT_DIR

# Compile to object file

# bootloader
nasm -f elf32 $SOURCE_DIR/bootloader.asm -o $OUTPUT_DIR/bootloader.o

# compile all .c files
# Compile each source file
for FILE in $SOURCE_DIR/*.c; do
    FILENAME=$(basename $FILE)
    OBJECT_NAME="${FILENAME%.*}.o"
    gcc $CFLAGS -o $OUTPUT_DIR/$OBJECT_NAME $FILE
done

# gcc $CFLAGS -o $OUTPUT_DIR/io.o $SOURCE_DIR/io.c
# gcc $CFLAGS -o $OUTPUT_DIR/ata.o $SOURCE_DIR/ata.c
# gcc $CFLAGS -o $OUTPUT_DIR/cmos.o $SOURCE_DIR/cmos.c
# gcc $CFLAGS -o $OUTPUT_DIR/gdt.o $SOURCE_DIR/gdt.c
# gcc $CFLAGS -o $OUTPUT_DIR/idt.o $SOURCE_DIR/idt.c
# gcc $CFLAGS -o $OUTPUT_DIR/irq.o $SOURCE_DIR/irq.c
# gcc $CFLAGS -o $OUTPUT_DIR/isr.o $SOURCE_DIR/isr.c
# gcc $CFLAGS -o $OUTPUT_DIR/pit.o $SOURCE_DIR/pit.c
# gcc $CFLAGS -o $OUTPUT_DIR/memory.o $SOURCE_DIR/memory.c
# gcc $CFLAGS -o $OUTPUT_DIR/video.o $SOURCE_DIR/video.c
# gcc $CFLAGS -o $OUTPUT_DIR/keyboard.o $SOURCE_DIR/keyboard.c
# gcc $CFLAGS -o $OUTPUT_DIR/kernel.o $SOURCE_DIR/kernel.c
# gcc $CFLAGS -o $OUTPUT_DIR/strings.o $SOURCE_DIR/strings.c
# gcc $CFLAGS -o $OUTPUT_DIR/system.o $SOURCE_DIR/system.c
# gcc $CFLAGS -o $OUTPUT_DIR/fat32.o $SOURCE_DIR/fat32.c

# Link using the linker script
ld -m elf_i386 -T kernel.ld -nostdlib -o $OUTPUT_DIR/kernel.bin \
    $OUTPUT_DIR/bootloader.o \
    $OUTPUT_DIR/kernel.o \
    $OUTPUT_DIR/io.o \
    $OUTPUT_DIR/video.o \
    $OUTPUT_DIR/strings.o \
    $OUTPUT_DIR/fat32.o \
    $OUTPUT_DIR/ata.o \
    $OUTPUT_DIR/keyboard.o \
    $OUTPUT_DIR/cmos.o \
    $OUTPUT_DIR/stdlib.o \
    $OUTPUT_DIR/idt.o \
    $OUTPUT_DIR/isr.o \
    $OUTPUT_DIR/irq.o \
    $OUTPUT_DIR/pit.o \
    $OUTPUT_DIR/gdt.o \
    $OUTPUT_DIR/system.o \
    $OUTPUT_DIR/stdio.o

# ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli_date.elf \
#     $OUTPUT_DIR/cli_date.o \
#     $OUTPUT_DIR/io.o \
#     $OUTPUT_DIR/video.o \
#     $OUTPUT_DIR/strings.o \
#     $OUTPUT_DIR/ata.o \
#     $OUTPUT_DIR/fat32.o \
#     $OUTPUT_DIR/system.o \
#     $OUTPUT_DIR/cmos.o \
#     $OUTPUT_DIR/stdlib.o \
#     $OUTPUT_DIR/stdio.o

# ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli_dir.elf $OUTPUT_DIR/cli_dir.o \
#     $OUTPUT_DIR/io.o \
#     $OUTPUT_DIR/video.o \
#     $OUTPUT_DIR/strings.o \
#     $OUTPUT_DIR/ata.o \
#     $OUTPUT_DIR/fat32.o \
#     $OUTPUT_DIR/cmos.o \
#     $OUTPUT_DIR/stdlib.o \
#     $OUTPUT_DIR/system.o \
#     $OUTPUT_DIR/stdio.o

# ld -m elf_i386 -T linkprg.ld -nostdlib -o $OUTPUT_DIR/cli_test.elf $OUTPUT_DIR/cli_test.o \
#     $OUTPUT_DIR/io.o \
#     $OUTPUT_DIR/video.o \
#     $OUTPUT_DIR/strings.o \
#     $OUTPUT_DIR/ata.o \
#     $OUTPUT_DIR/fat32.o \
#     $OUTPUT_DIR/cmos.o \
#     $OUTPUT_DIR/stdlib.o \
#     $OUTPUT_DIR/system.o \
#     $OUTPUT_DIR/stdio.o

# Convert to binary format
# objcopy -O binary $OUTPUT_DIR/cli_date.elf $OUTPUT_DIR/date.prg
# objcopy -O binary $OUTPUT_DIR/cli_dir.elf $OUTPUT_DIR/dir.prg
# objcopy -O binary $OUTPUT_DIR/cli_test.elf $OUTPUT_DIR/test.prg

#sudo ./make_image.sh

# sudo mount ../disk.img /mnt/disk
# sudo cp $OUTPUT_DIR/date.prg /mnt/disk/sys
# sudo cp $OUTPUT_DIR/dir.prg /mnt/disk/sys
# sudo cp $OUTPUT_DIR/test.prg /mnt/disk/sys
# sudo umount /mnt/disk

# Create the iso file for kernel
mkdir -p $ISO_DIR/boot/grub
cp $OUTPUT_DIR/kernel.bin $ISO_DIR/boot/
cp grub.cfg $ISO_DIR/boot/grub/
grub-mkrescue -o kernel.iso $ISO_DIR/

#./run.sh
