// ATA driver
#include "ata.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"
#include "toolchain/string.h"
#include "toolchain/stdlib.h"
#include "drivers/fdd/fdd.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


drive_t* current_drive = NULL;// = {0};  // Current drive (global variable)
drive_t detected_drives[MAX_DRIVES];  // Global array of detected drives
short drive_count = 0;  // Number of detected drives


bool wait_for_drive_ready(unsigned short base, unsigned int timeout_ms) {
    unsigned int elapsed_time = 0;
    while (inb(ATA_STATUS(base)) & 0x80) {  // Wait while BSY bit is set
        if (elapsed_time >= timeout_ms) {
            printf("Timeout: Drive not ready within %u ms.\n", timeout_ms);
            return false;  // Timeout reached
        }
        delay_ms(1);  // Yield CPU for 1 ms
        elapsed_time += 1;  // Increment elapsed time by 1 ms
    }
    return true;  // Drive is ready
}

bool wait_for_drive_data_ready(unsigned short base, unsigned int timeout_ms) {
    unsigned int elapsed_time = 0;
    while (!(inb(ATA_STATUS(base)) & 0x08)) {  // Wait for DRQ bit to set
        if (elapsed_time >= timeout_ms) {
            printf("Timeout: Drive data not ready within %u ms.\n", timeout_ms);
            return false;  // Timeout reached
        }
        delay_ms(1);  // Yield CPU for 1 ms
        elapsed_time += 1;  // Increment elapsed time by 1 ms
    }
    return true;  // Data is ready
}

/*
    * Reads a sector from the ATA drive.
    * 
    * @param lba The Logical Block Addressing of the sector to read.
    * @param buffer The buffer to read the sector into.
    * @return True if the sector was read successfully, false otherwise.
*/
bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master) {
    // Wait for the drive to be ready
    // while (inb(ATA_STATUS(base)) & 0x80) {
    //     // Sleep for 1 millisecond
    //     delay_ms(1);
    // }
    if (!wait_for_drive_ready(base, 1000)) {  // 1000 ms timeout
        return false;  // Drive not ready within the timeout
    }

    // Set up sector count and LBA
    outb(ATA_SECTOR_CNT(base), 1); // Read 1 sector
    outb(ATA_LBA_LOW(base), (unsigned char)(lba & 0xFF));
    outb(ATA_LBA_MID(base), (unsigned char)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH(base), (unsigned char)((lba >> 16) & 0xFF));

    // Set the drive/head register for LBA mode
    unsigned char drive_head = 0xE0 | ((lba >> 24) & 0x0F); // LBA mode with upper LBA bits
    drive_head |= is_master ? 0x00 : 0x10; // 0x00 for master, 0x10 for slave
    outb(ATA_DRIVE_HEAD(base), drive_head);

    // Send the read command
    outb(ATA_COMMAND(base), ATA_READ_SECTORS);

    // Wait for the drive to be ready to transfer data
    // while (!(inb(ATA_STATUS(base)) & 0x08)) {
    //     // Sleep for 1 millisecond
    //     delay_ms(1);
    // }
    if (!wait_for_drive_ready(base, 1000)) {  // 1000 ms timeout
        return false;  // Drive not ready within the timeout
    }

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
bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer, bool is_master) {
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
    int drive_name_index = 0;  // For generating names like "hdd1", "hdd2", etc.

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

                // Trim trailing spaces from the model name
                trim_trailing_spaces(ata_drive_info->model);

                ata_drive_info->type = DRIVE_TYPE_ATA;
                snprintf(ata_drive_info->name, sizeof(ata_drive_info->name), "hdd%d", drive_name_index++);
                printf("ATA drive %s detected: %s, Sectors: %u\n", ata_drive_info->name, ata_drive_info->model, ata_drive_info->sectors);

                // Initialize the file system for the detected drive
                //init_fs(ata_drive_info);

                // Increment the global drive count after successfully adding a drive
                drive_count++;
            }
        }
    }
}

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

    // Sanity checks for valid IDENTIFY response
    if (identify_data[0] == 0 || identify_data[0] == 0xFFFF) {
        return false;  // Invalid or non-ATA device
    }

    // Extract the model number (bytes 54-93 in IDENTIFY data)
    for (int i = 0; i < 20; i++) {
        drive_info->model[i * 2] = identify_data[27 + i] >> 8;
        drive_info->model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    drive_info->model[40] = '\0';

    // Validate the model name (check for printable characters)
    for (int i = 0; i < 40; i++) {
        if (drive_info->model[i] != '\0' && (drive_info->model[i] < 32 || drive_info->model[i] > 126)) {
            return false;  // Invalid model name, likely corrupted data
        }
    }

    // Get the total sector count (LBA28; words 60-61)
    drive_info->sectors = identify_data[60] | (identify_data[61] << 16);

    // Validate sector count
    if (drive_info->sectors <= 0) {
        return false;  // Invalid sector count, likely corrupted data
    }

    return true;
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
