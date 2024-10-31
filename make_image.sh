#!/bin/bash

# Create the floppy image file for kernel
#dd if=/dev/zero of=fdd.img bs=1024 count=1440

# Format the floppy image file
#mkfs.vfat fdd.img

# sudo mkdir /mnt/floppy
# sudo mount -o loop fdd.img /mnt/floppy

#create a harddisk image file
sudo dd if=/dev/zero of=./disk.img bs=1M count=50
sudo mkfs.fat -F 32 ./disk.img

#mount the harddisk image file
sudo mount ./disk.img /mnt/disk
sudo mkdir /mnt/disk/sys
sudo mkdir /mnt/disk/sys/bin
sudo mkdir /mnt/disk/sys/boot
sudo mkdir /mnt/disk/sys/home
sudo umount /mnt/disk
