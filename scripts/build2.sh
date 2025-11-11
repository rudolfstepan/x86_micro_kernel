#!/bin/bash

# Set output directories
OUTPUT_DIR=build
ISO_DIR=iso

# Create output directories if they don't exist
mkdir -p $OUTPUT_DIR
mkdir -p $ISO_DIR

# Build binaries
build_binaries() {
  # Link object files into ELF format
  ld -m elf_i386 -T linkprg.ld \
    -nostdlib \
    -o $OUTPUT_DIR/cli/cli_date.elf \
    $OUTPUT_DIR/cli/cli_date.o \
    $OUTPUT_DIR/drivers/rtc/rtc.o \
    $OUTPUT_DIR/drivers/io/io.o \
    $OUTPUT_DIR/drivers/video/video.o \
    $OUTPUT_DIR/drivers/ata/ata.o \
    $OUTPUT_DIR/toolchain/stdlib.o \
    $OUTPUT_DIR/toolchain/stdio.o \
    $OUTPUT_DIR/toolchain/strings.o \
    $OUTPUT_DIR/filesystem/fat32.o

  # Convert ELF to binary format
  objcopy -O binary $OUTPUT_DIR/cli/cli_date.elf $OUTPUT_DIR/cli/date.prg
  objcopy -O binary $OUTPUT_DIR/cli/cli_test.elf $OUTPUT_DIR/cli/test.prg
}

# Mount disk image and copy binaries
mount_disk_image() {
  # Check if directory exists, create it if not
  if [ ! -d /mnt/disk ]; then
    echo "Creating /mnt/disk directory"
    mkdir -p /mnt/disk
  fi

  # Mount virtual disk image
  sudo mount ../disk.img /mnt/disk

  # Copy generated binaries to disk image
  sudo cp $OUTPUT_DIR/cli/date.prg /mnt/disk/sys
  sudo cp $OUTPUT_DIR/cli/test.prg /mnt/disk/sys

  # Unmount disk image
  sudo umount /mnt/disk
}

# Prepare ISO for GRUB
prepare_iso() {
  # Set up directory structure for ISO file system
  mkdir -p $ISO_DIR/boot/grub

  # Copy kernel binary and GRUB configuration to ISO file system
  cp $OUTPUT_DIR/kernel.bin $ISO_DIR/boot/
  cp grub.cfg $ISO_DIR/boot/grub/

  # Create ISO image using GRUB
  sudo grub-mkrescue -o kernel.iso $ISO_DIR
}

# Run main script
main() {
  build_binaries
  mount_disk_image
  prepare_iso
}

# Call main function
main