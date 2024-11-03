#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"

#define ROOT_ENTRY_SIZE 32  // Each root directory entry is 32 bytes
#define SECTOR_SIZE 512
#define ROOT_DIR_SECTORS 14 // Number of sectors for the root directory on a 1.44MB floppy

FAT12 fat12_fs;
BootSector f12_boot_sector;

// Function to read the boot sector and initialize the FAT12 structure
bool fat12_init(int drive) {
    uint8_t sector[SECTOR_SIZE];

    // Read the boot sector
    if (!fdd_read_sector(drive, 0, 0, 1, sector)) {
        printf("Failed to read boot sector.\n");
        return false;
    }

    // Copy the boot sector into the FAT12 structure
    fat12_fs.bootSector = *(BootSector*)sector;

    // Calculate the starting sectors for each region
    fat12_fs.fatStart = fat12_fs.bootSector.reservedSectors; // FAT region starts right after reserved sectors
    fat12_fs.rootDirStart = fat12_fs.fatStart + (fat12_fs.bootSector.fatCount * fat12_fs.bootSector.sectorsPerFAT);
    fat12_fs.dataStart = fat12_fs.rootDirStart + ((fat12_fs.bootSector.rootEntryCount * ROOT_ENTRY_SIZE + (fat12_fs.bootSector.bytesPerSector - 1)) / fat12_fs.bootSector.bytesPerSector);

    printf("FAT12 Initialization Complete:\n");
    printf("Bytes per Sector: %u\n", fat12_fs.bootSector.bytesPerSector);
    printf("Sectors per Cluster: %u\n", fat12_fs.bootSector.sectorsPerCluster);
    printf("Number of FATs: %u\n", fat12_fs.bootSector.fatCount);
    printf("Root Directory Entries: %u\n", fat12_fs.bootSector.rootEntryCount);
    printf("Sectors per FAT: %u\n", fat12_fs.bootSector.sectorsPerFAT);
    printf("FAT Start Sector: %d\n", fat12_fs.fatStart);
    printf("Root Directory Start Sector: %d\n", fat12_fs.rootDirStart);
    printf("Data Start Sector: %d\n", fat12_fs.dataStart);

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

// Function to read the boot sector
bool read_boot_sector(int drive) {
    return fdd_read_sector(drive, 0, 0, 1, (uint8_t*)&f12_boot_sector);
}

// Function to read the Root Directory
bool read_root_directory(int drive, DirectoryEntry* root_directory) {
    for (int i = 0; i < ROOT_DIR_SECTORS; i++) {
        if (!fdd_read_sector(drive, 0, 1, 2 + i, ((uint8_t*)root_directory) + i * SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

// Function to display the Root Directory
void display_root_directory(DirectoryEntry* root_directory, int num_entries) {
    printf("Inhaltsverzeichnis der Diskette:\n");
    printf("Dateiname    | Erweiterung | Groesse (Bytes)\n");
    printf("--------------------------------------------\n");

    for (int i = 0; i < num_entries; i++) {
        if ((signed char)root_directory[i].filename[0] == (signed char)0x00) break;  // No more entries
        if ((signed char)root_directory[i].filename[0] == (signed char)0xE5) continue;  // Deleted entry

        // Format filename and extension
        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)root_directory[i].filename, 8);
        strncpy(extension, (const char*)root_directory[i].extension, 3);

        // Display file information
        //printf("%-11s | %-3s        | %10u\n", filename, extension, root_directory[i].fileSize);
        printf("%s | %s        | %u\n", filename, extension, root_directory[i].fileSize);
    }
}

// Function to read the contents of a directory in the FAT12 file system
bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
    printf("Reading FAT12 directory: %s\n", path);

    int drive = 0;  // Drive A:
    
    DirectoryEntry root_directory[224];  // Standard size of Root Directory for a 1.44MB floppy

    // Read the boot sector into the global boot_sector variable
    if (!read_boot_sector(drive)) {
        printf("Error reading the boot sector.\n");
        return false;
    }

    // Read the Root Directory into a local buffer
    if (!read_root_directory(drive, root_directory)) {
        printf("Error reading the Root Directory.\n");
        return false;
    }

    printf("Root Directory Read Complete.\n");
    printf("Root Directory Entries: %u\n", fat12_fs.bootSector.rootEntryCount);

    printf("Displaying Root Directory:\n");
    printf("%s\n", root_directory);

    // Display the Root Directory
    display_root_directory(root_directory, fat12_fs.bootSector.rootEntryCount);

    return true;
}