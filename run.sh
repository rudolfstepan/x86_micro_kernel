#!/bin/bash

#load the kernel and the harddisk image as raw format -M pc-i440fx-1.5
qemu-system-x86_64 -vga std -boot d -cdrom ./kernel.iso \
  -drive file=./disk.img,format=raw \
  -drive file=./disk1.img,format=raw \
  -drive file=./floppy.img,format=raw,if=floppy \
  -drive file=./fdd.img,format=raw,if=floppy

# qemu-system-x86_64 -boot d -cdrom ./kernel.iso \
#   -drive file=./disk.img,format=raw \
#   -drive file=./disk1.img,format=raw \
#   -drive file=../../msdos_6.22_de_disk1.img,format=raw,if=floppy
