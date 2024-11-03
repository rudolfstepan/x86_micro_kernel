#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#define ROOT_ENTRY_SIZE 32  // Each root directory entry is 32 bytes
#define SECTOR_SIZE 512
#define ROOT_DIR_SECTORS 14 // Number of sectors for the root directory on a 1.44MB floppy

FAT12 fat12;  // Global instance of FAT12 structure

// Function to read the boot sector
bool read_boot_sector(int drive) {

    uint8_t buffer[SECTOR_SIZE];


    fdd_read_sector(drive, 0, 0, 1, buffer);

    // Copy the boot sector into the FAT12 structure
    memcpy(&fat12.bootSector, buffer, sizeof(Fat12BootSector));

    printf("%s", buffer);

    // Validate the boot sector signature
    if (fat12.bootSector.bootSectorSignature != 0xAA55) {
        printf("Invalid boot sector signature: 0x%04X\n", fat12.bootSector.bootSectorSignature);
        return false;
    }


    return true;
}

// Function to read the boot sector and initialize the FAT12 structure
bool fat12_init(int drive) {

    // Zero-initialize FAT12 structure
    memset(&fat12, 0, sizeof(FAT12));

    if (!read_boot_sector(drive)) {
        printf("Failed to read boot sector.\n");
        return false;
    }

    // Calculate start positions
    fat12.fatStart = fat12.bootSector.reservedSectors;
    fat12.rootDirStart = fat12.fatStart + (fat12.bootSector.fatCount * fat12.bootSector.sectorsPerFAT);
    fat12.dataStart = fat12.rootDirStart + ((fat12.bootSector.rootEntryCount * ROOT_ENTRY_SIZE + fat12.bootSector.bytesPerSector - 1) / fat12.bootSector.bytesPerSector);

    printf("FAT12 Initialization Complete:\n");
    printf("Bytes per Sector: %u\n", fat12.bootSector.bytesPerSector);
    printf("Sectors per Cluster: %u\n", fat12.bootSector.sectorsPerCluster);
    printf("Number of FATs: %u\n", fat12.bootSector.fatCount);
    printf("Root Directory Entries: %u\n", fat12.bootSector.rootEntryCount);
    printf("Sectors per FAT: %u\n", fat12.bootSector.sectorsPerFAT);
    printf("OEM Name: %s\n", fat12.bootSector.oemName);
    printf("Volume Label: %s\n", fat12.bootSector.volumeLabel);
    printf("File System Type: %s\n", fat12.bootSector.fsType);
    printf("Boot Sector Signature: 0x%04X\n", fat12.bootSector.bootSectorSignature);

    return true;
}

// Function to initialize the FAT12 file system
bool fat12_init_fs() {
    if (!fat12_init(0)) {
        printf("Failed to initialize FAT12 file system.\n");
        return false;
    }
    return true;
}

// Function to read the Root Directory
bool read_root_directory(int drive, DirectoryEntry* root_directory) {
    for (int i = 0; i < ROOT_DIR_SECTORS; i++) {
        if (!fdd_read_sector(drive, 0, 1, fat12.rootDirStart + i, ((void*)root_directory) + i * SECTOR_SIZE)) {
            printf("Error reading Root Directory sector %d\n", i);
            return false;
        }
    }
    return true;
}

bool is_directory(DirectoryEntry* entry) {
    return (entry->attributes & 0x10) != 0;  // Checks if the directory attribute is set
}

bool is_file(DirectoryEntry* entry) {
    return (entry->attributes & 0x20) != 0;  // Checks if the archive attribute is set
}

// Function to display the Root Directory
void display_root_directory(DirectoryEntry* root_directory, int num_entries) {
    printf("Inhaltsverzeichnis der Diskette:\n");
    printf("Dateiname    | Erweiterung | Groesse (Bytes)\n");
    printf("--------------------------------------------\n");

    for (int i = 0; i < num_entries; i++) {
        DirectoryEntry* entry = &root_directory[i];

        if ((signed char)root_directory[i].filename[0] == (signed char)0x00) break;  // No more entries
        if ((signed char)root_directory[i].filename[0] == (signed char)0xE5) continue;  // Deleted entry

        // Format filename and extension
        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)root_directory[i].filename, 8);
        strncpy(extension, (const char*)root_directory[i].extension, 3);

        // Display file information
        if (is_directory(entry)) {
            printf("Directory: %s\n", filename);
        } else if (is_file(entry)) {
            printf("File: %.8s.%.3s - Size: %u bytes\n", filename, extension, entry->fileSize);
        }
    }
}

// Function to read the contents of a directory in the FAT12 file system
bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
    printf("Reading FAT12 directory: %s\n", path);

    int drive = 0;  // Drive A:
    
    DirectoryEntry root_directory[256];  // Standard size of Root Directory for a 1.44MB floppy

    // Read the Root Directory into a local buffer
    if (!read_root_directory(drive, root_directory)) {
        printf("Error reading the Root Directory.\n");
        return false;
    }

    // Display the Root Directory
    display_root_directory(root_directory, fat12.bootSector.rootEntryCount);

    return true;
}
