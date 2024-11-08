#include "filesystem.h"
#include "drivers/ata/ata.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "toolchain/stdio.h"
#include "filesystem/fat32/fat32.h"
#include <stddef.h>

// Function to initialize the file system on a given drive
void init_fs(drive_t* drive) {
    if (drive->type == DRIVE_TYPE_ATA) {
        //printf("Init fs on ATA drive %s: %s with %u sectors\n", drive->name, drive->model, drive->sectors);
        // Initialize file system for ATA drive
        // Read the boot sector
        BootSector* boot_sector = (BootSector*)malloc(sizeof(BootSector));
        
        if (!ata_read_sector(drive->base, 0, boot_sector, drive->is_master)) {
            printf("Failed to read boot sector.\n");
            return;
        }

        // Create a local, null-terminated copy of fsType
        char fs_type[9] = {0};  // 8 bytes + 1 for null terminator
        memcpy(fs_type, boot_sector->fsType, 8);
        fs_type[8] = '\0';  // Ensure null termination

        // Detect filesystem type by examining the boot sector
        if (strcmp(fs_type, "FAT12   ") == 0) {
            printf("Detected FAT12 filesystem on drive %s.\n", drive->name);
            //fat12_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT16   ") == 0) {
            printf("Detected FAT16 filesystemo n drive %s.\n", drive->name);
            //fat16_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT32   ") == 0) {
            printf("Detected FAT32 filesystem on drive %s.\n", drive->name);
            fat32_init_fs(drive->base, drive->is_master);
        } else if (memcmp(boot_sector->oemName, "NTFS    ", 8) == 0) {
            printf("Detected NTFS filesystem on drive %s.\n", drive->name);
            //ntfs_init_fs(drive->base, drive->is_master);
        } else {
            printf("Unknown or unsupported filesystem.\n");
        }

        free(boot_sector);
        boot_sector = NULL;
    }
}
