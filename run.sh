#!/bin/bash

#load the kernel and the harddisk image as raw format -M pc-i440fx-1.5
qemu-system-x86_64 -machine pc -boot d -cdrom ./kernel.iso \
  -drive file=./disk.img,format=raw \
  -drive file=./disk1.img,format=raw \
  -drive file=./floppy.img,format=raw,if=floppy \
  -m 512M -cpu qemu32 
