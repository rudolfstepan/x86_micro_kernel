#!/bin/bash

# create a blank 1.44MB disk image file
dd if=/dev/zero of=floppy.img bs=512 count=2880
# Format the floppy image file
mkfs.fat -F 12 ./floppy.img

# Check if /mnt/floppy directory exists; create it if it doesn't
if [ ! -d "/mnt/floppy" ]; then
    sudo mkdir /mnt/floppy
fi

sudo mount -o loop fdd.floppy /mnt/floppy
sudo mkdir /mnt/floppy/abc
sudo umount /mnt/floppy

#create a harddisk image file
sudo dd if=/dev/zero of=./disk.img bs=1M count=50
sudo mkfs.fat -F 32 ./disk.img

#mount the 1st harddisk image file
# Check if /mnt/disk directory exists; create it if it doesn't
if [ ! -d "/mnt/disk" ]; then
    sudo mkdir /mnt/disk
fi

sudo mount ./disk.img /mnt/disk
sudo mkdir /mnt/disk/sys
sudo mkdir /mnt/disk/sys/bin
sudo mkdir /mnt/disk/sys/boot
sudo mkdir /mnt/disk/sys/home
sudo umount /mnt/disk

#create a harddisk image file
sudo dd if=/dev/zero of=./disk1.img bs=1M count=50
sudo mkfs.fat -F 32 ./disk1.img

#mount the 2nd harddisk image file
# Check if /mnt/disk directory exists; create it if it doesn't
if [ ! -d "/mnt/disk1" ]; then
    sudo mkdir /mnt/disk1
fi

sudo mount ./disk1.img /mnt/disk1
sudo mkdir /mnt/disk1/hello
sudo umount /mnt/disk1
