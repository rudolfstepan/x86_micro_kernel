#include <stddef.h>

#include "ata.h"
#include "io.h"

/*
    * Reads a sector from the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to read.
    * @param buffer The buffer to read the sector into.
    * @return True if the sector was read successfully, false otherwise.
*/
bool read_sector(unsigned int lba, void* buffer) {
    // Wait for the drive to be ready
    while (inb(ATA_STATUS) & 0x80) {}

    outb(ATA_SECTOR_CNT, 1); // Read 1 sector
    outb(ATA_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));

    // Send the read command
    outb(ATA_COMMAND, ATA_READ_SECTORS);

    // Wait for the drive to be ready to transfer data
    while (!(inb(ATA_STATUS) & 0x08)) {}

    // Read the data
    insw(ATA_DATA, buffer, SECTOR_SIZE / 2);

    return true;
}

/*
    * Writes a sector to the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to write.
    * @param buffer The buffer to write to the sector.
    * @return True if the sector was written successfully, false otherwise.
*/
bool write_sector(unsigned int lba, const void* buffer) {
    if (buffer == NULL) {
        return false; // Error: Buffer is null
    }

    // Wait for the drive to be ready
    while (inb(ATA_STATUS) & 0x80) {}

    // Set up the sector count, LBA (Logical Block Addressing) and drive/head
    outb(ATA_SECTOR_CNT, 1); // Write 1 sector
    outb(ATA_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));

    // Send the write command
    outb(ATA_COMMAND, ATA_WRITE_SECTORS);

    // Wait for the drive to signal that it's ready to receive data
    while (!(inb(ATA_STATUS) & 0x08)) {}

    // Write the data
    outsw(ATA_DATA, buffer, SECTOR_SIZE / 2);

    return true;
}