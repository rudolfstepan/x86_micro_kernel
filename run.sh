#!/bin/bash

#load the kernel and the harddisk image as raw format -M pc-i440fx-1.5
#qemu-system-x86_64 -boot d -cdrom ./kernel.iso -drive file=./disk.img,format=raw -drive file=./disk1.img,format=raw

qemu-system-x86_64 -boot d -cdrom ./kernel.iso -drive file=./disk.img,format=raw -drive file=./fdd.img,format=raw
