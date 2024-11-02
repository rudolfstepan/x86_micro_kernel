// ATA driver
#include "ata.h"
#include <stddef.h>
#include "drivers/io/io.h"
#include "toolchain/stdio.h"
#include "drivers/fdd/fdd.h"

drive_t* current_drive = {0};  // Current drive (global variable)

drive_t detected_drives[MAX_DRIVES];  // Global array of detected drives
int drive_count = 0;  // Global count of detected drives

/*
    * Reads a sector from the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to read.
    * @param buffer The buffer to read the sector into.
    * @return True if the sector was read successfully, false otherwise.
*/
bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master) {
    // Wait for the drive to be ready
    while (inb(ATA_STATUS(base)) & 0x80) {}

    // Set up sector count and LBA
    outb(ATA_SECTOR_CNT(base), 1); // Read 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Set the drive/head register
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F);  // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10;  // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);

    // Send the read command
    outb(ATA_COMMAND(base), ATA_READ_SECTORS);

    // Wait for the drive to be ready to transfer data
    while (!(inb(ATA_STATUS(base)) & 0x08)) {}

    // Read the data
    insw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);

    return true;
}

/*
    * Writes a sector to the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to write.
    * @param buffer The buffer to write to the sector.
    * @return True if the sector was written successfully, false otherwise.
*/
bool ata_write_sector(unsigned short base, unsigned int lba, const void* buffer, bool is_master) {
    if (buffer == NULL) {
        return false; // Error: Buffer is null
    }

    // Wait for the drive to be ready
    while (inb(ATA_STATUS(base)) & 0x80) {}

    // Set up the sector count, LBA (Logical Block Addressing), and drive/head
    outb(ATA_SECTOR_CNT(base), 1); // Write 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Set the drive/head register
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F);  // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10;  // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);

    // Send the write command
    outb(ATA_COMMAND(base), ATA_WRITE_SECTORS);

    // Wait for the drive to signal that it's ready to receive data
    while (!(inb(ATA_STATUS(base)) & 0x08)) {}

    // Write the data
    outsw(ATA_DATA(base), buffer, SECTOR_SIZE / 2);

    return true;
}

drive_t* ata_get_drive(unsigned short drive_index) {
    if (drive_index >= drive_count) {
        return NULL;  // Return NULL if the index is out of bounds
    }
    return &detected_drives[drive_index];
}

// Function to detect all ATA drives on the primary and secondary buses
void ata_detect_drives() {
    uint16_t bases[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    uint8_t drives[2] = { ATA_MASTER, ATA_SLAVE };
    int drive_name_index = 1;  // For generating names like "hdd1", "hdd2", etc.

    drive_count = 0;  // Reset drive count before detection

    // Detect ATA drives
    for (int bus = 0; bus < 2; bus++) {
        for (int drive = 0; drive < 2; drive++) {
            if (drive_count >= MAX_DRIVES) {
                printf("Maximum number of drives reached.\n");
                return;
            }

            drive_t* ata_drive_info = &detected_drives[drive_count];
            ata_drive_info->base = bases[bus];
            ata_drive_info->is_master = (drive == 0);  // 0 for master, 1 for slave

            // Attempt to identify the drive
            if (ata_identify_drive(bases[bus], drives[drive], ata_drive_info)) {
                ata_drive_info->type = DRIVE_TYPE_ATA;
                snprintf(ata_drive_info->name, sizeof(ata_drive_info->name), "hdd%d", drive_name_index++);
                printf("ATA drive %s detected: %s, Sectors: %u\n", ata_drive_info->name, ata_drive_info->model, ata_drive_info->sectors);

                // Initialize the file system for the detected drive
                init_fs(ata_drive_info);

                // Increment the global drive count after successfully adding a drive
                drive_count++;
            }
        }
    }

    // Detect floppy drives
    detect_fdd();
}

// Function to send the IDENTIFY command and retrieve drive information
bool ata_identify_drive(uint16_t base, uint8_t drive, drive_t *drive_info) {
    // Select the drive (master or slave)
    outb(base + 6, drive);

    // Send the IDENTIFY command
    outb(base + 7, ATA_IDENTIFY);

    // Wait a bit for the drive to respond
    if (inb(base + 7) == 0) {
        return false;  // No drive present
    }

    // Wait until BSY clears and DRQ sets
    while (inb(base + 7) & 0x80) {}    // Wait for BSY to clear
    if (!(inb(base + 7) & 0x08)) {
        return false;  // DRQ not set, not an ATA device
    }

    // Read the IDENTIFY data
    uint16_t identify_data[256];
    insw(base, identify_data, 256);

    // Extract the model number (bytes 54-93 in IDENTIFY data)
    for (int i = 0; i < 20; i++) {
        drive_info->model[i * 2] = identify_data[27 + i] >> 8;
        drive_info->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    drive_info->model[40] = '\0';

    // Get the total sector count (LBA28; words 60-61)
    drive_info->sectors = identify_data[60] | (identify_data[61] << 16);

    return true;
}

// Function to initialize the file system on a given drive
void init_fs(drive_t* drive) {
    if (drive->type == DRIVE_TYPE_ATA) {
        printf("Init fs on ATA drive %s: %s with %u sectors\n", drive->name, drive->model, drive->sectors);
        // Initialize file system for ATA drive
        fat32_init_fs(drive->base, drive->is_master);
    } else if (drive->type == DRIVE_TYPE_FDD) {
        // printf("Init fs on FDD %s with CHS %u/%u/%u\n", drive->name, drive->cylinder, drive->head, drive->sector);
        // // Initialize file system or handling code for FDD
        // // Call fat12_init_fs as part of FDD initialization
        // if (fat12_init_fs()) {
        //     printf("FAT12 file system initialized successfully.\n");
        // } else {
        //     printf("FAT12 initialization failed.\n");
        // }

    } else {
        printf("Unknown drive type, skipping initialization.\n");
    }
}

drive_t* get_drive_by_name(const char* name) {
    for (int i = 0; i < drive_count; i++) {
        if (strcmp(detected_drives[i].name, name) == 0) {
            return &detected_drives[i];
        }
    }
    return NULL;  // Return NULL if no drive with the specified name is found
}

void list_detected_drives() {
    for (int i = 0; i < drive_count; i++) {
        drive_t* drive = &detected_drives[i];
        printf("Drive %s: Model %s, Sectors %u\n", drive->name, drive->model, drive->sectors);
    }
}

void detect_fdd() {
    // Turn on the motor for FDD 0 (assumes a single FDD attached)
    outb(FDD_DOR, 0x1C);  // 0x1C = motor on, select drive 0

    // Wait briefly for the motor to stabilize
    for (volatile int i = 0; i < 10000; i++);

    // Check the FDD status to confirm if an FDD is present
    if (inb(FDD_MSR) & 0x80) {  // Bit 7 set if FDC is ready
        drive_t* drive = &detected_drives[drive_count++];
        drive->type = DRIVE_TYPE_FDD;
        snprintf(drive->name, sizeof(drive->name), "fdd%d", 1);
        drive->cylinder = 80;  // Common for 1.44MB floppies
        drive->head = 2;
        drive->sector = 18;
        printf("Floppy drive detected: %s\n", drive->name);
    }

    // Turn off the motor
    outb(FDD_DOR, 0x0C);
}