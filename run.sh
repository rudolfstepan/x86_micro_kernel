#!/bin/bash

#load the kernel and the harddisk image as raw format -M pc-i440fx-1.5
qemu-system-x86_64 -m 512M -boot d -cdrom ./kernel.iso -drive file=./disk.img,format=raw -drive file=./floppy.img,format=raw,if=floppy -netdev tap,id=net0,ifname=tap0,script=no,downscript=no -device ne2k_pci,netdev=net0