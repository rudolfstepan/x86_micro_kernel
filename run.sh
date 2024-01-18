#!/bin/bash

#load the kernel and the harddisk image as raw format -M pc-i440fx-1.5
qemu-system-x86_64 -rtc base=2024-01-08T10:10:00 -boot d -cdrom ./kernel.iso -drive file=../disk.img,format=raw

