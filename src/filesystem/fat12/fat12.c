#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#define ROOT_ENTRY_SIZE 32   // Each root directory entry is 32 bytes
#define SECTOR_SIZE 512      // Standard sector size for FAT12 on floppy
#define ROOT_DIR_SECTORS 14  // Number of sectors for the root directory on a 1.44MB floppy

int read_fat12(uint8_t drive, FAT12* fat12) {
    // Temporary buffer to hold sector data
    uint8_t buffer[SECTOR_SIZE];

    // Step 1: Read the boot sector (First sector of the disk)
    if (!fdc_read_sector(drive, 0, 0, 1, buffer)) {
        printf("Error reading boot sector.\n");
        return false;  // Error reading the boot sector
    }

    // Copy boot sector data into FAT12 structure
    memcpy(&fat12->bootSector, buffer, sizeof(Fat12BootSector));

    // Verify the boot sector signature
    if (fat12->bootSector.bootSectorSignature != 0xAA55) {
        printf("Invalid boot sector signature.\n");
        return false;
    }

    // Step 2: Calculate the start of the FAT, root directory, and data regions
    fat12->fatStart = fat12->bootSector.reservedSectors;  // FAT starts after reserved sectors
    fat12->rootDirStart = fat12->fatStart + (fat12->bootSector.fatCount * fat12->bootSector.sectorsPerFAT);  // Root dir starts after FAT area
    fat12->dataStart = fat12->rootDirStart + ((fat12->bootSector.rootEntryCount * sizeof(DirectoryEntry)) / SECTOR_SIZE);  // Data area starts after root directory

    printf("FAT12 initialized:\n");
    printf("FAT Start Sector: %d\n", fat12->fatStart);
    printf("Root Directory Start Sector: %d\n", fat12->rootDirStart);
    printf("Data Region Start Sector: %d\n", fat12->dataStart);

    return true;  // Successfully initialized FAT12 structure
}

// Function to read the root directory
int read_root_directory(uint8_t drive, FAT12* fat12, DirectoryEntry* entries, int max_entries) {
    uint8_t buffer[SECTOR_SIZE];
    int root_dir_sector_count = (fat12->bootSector.rootEntryCount * sizeof(DirectoryEntry)) / SECTOR_SIZE;
    int entry_index = 0;

    for (int i = 0; i < root_dir_sector_count && entry_index < max_entries; i++) {
        // Calculate the sector to read based on the root directory start sector
        int sector = fat12->rootDirStart + i;

        // Read the sector into the buffer
        if (!fdc_read_sector(drive, sector / fat12->bootSector.sectorsPerTrack,
                            sector % fat12->bootSector.sectorsPerTrack, 1, buffer)) {
            return -1;  // Error reading sector
        }

        // Copy entries from the buffer to the entries array
        for (unsigned int j = 0; j < SECTOR_SIZE / sizeof(DirectoryEntry) && entry_index < max_entries; j++) {
            DirectoryEntry* entry = (DirectoryEntry*)(buffer + j * sizeof(DirectoryEntry));

            // Check if it's a valid entry (not empty or deleted)
            if (entry->filename[0] != 0x00 && entry->filename[0] != 0xE5) {
                memcpy(&entries[entry_index], entry, sizeof(DirectoryEntry));
                entry_index++;
            }
        }
    }

    return entry_index;  // Return the number of entries read
}

// Function to initialize the FAT12 file system
bool fat12_init_fs() {
    FAT12 fat12;  // Global instance of FAT12 structure

    if (read_fat12(0, &fat12)) {
        printf("FAT12 filesystem read successfully.\n");

        DirectoryEntry entries[16];

        int num_entries = read_root_directory(0, &fat12, entries, 16);

        printf("Number of entries to read: %d\n", num_entries);

        if (num_entries > 0) {
            printf("Root directory read successfully.\n");
            for (int i = 0; i < num_entries; i++) {
                DirectoryEntry* entry = &entries[i];

                // check if it's a directory
                if (entry->attributes & 0x10) {
                    printf("Directory: ");
                } else {
                    printf("File: ");
                }

                // Format filename and extension with null-terminators
                char filename[9] = {0};  // 8 chars + null terminator
                char extension[4] = {0}; // 3 chars + null terminator
                strncpy(filename, (const char*)entry->filename, 8);
                strncpy(extension, (const char*)entry->extension, 3);

                printf("Entry %d: %s.%s\n", i, filename, extension);
            }
        } else {
            printf("Error reading root directory.\n");
        }

    } else {
        printf("Failed to read FAT12 filesystem.\n");
        return false;
    }
    return false;
}

bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
    return false;
}

// // Function to display the Root Directory
// void display_root_directory(DirectoryEntry* root_directory, int num_entries) {
//     printf("Inhaltsverzeichnis der Diskette:\n");
//     printf("Dateiname    | Erweiterung | Groesse (Bytes)\n");
//     printf("--------------------------------------------\n");

//     for (int i = 0; i < num_entries; i++) {
//         DirectoryEntry* entry = &root_directory[i];

//         // Stop if the entry is unused
//         if ((unsigned char)entry->filename[0] == 0x00) {
//             break;  // No more entries
//         }

//         // Skip deleted entries
//         if ((unsigned char)entry->filename[0] == 0xE5) {
//             continue;
//         }

//         // Format filename and extension with null-terminators
//         char filename[9] = {0};  // 8 chars + null terminator
//         char extension[4] = {0}; // 3 chars + null terminator
//         strncpy(filename, (const char*)entry->filename, 8);
//         strncpy(extension, (const char*)entry->extension, 3);

//         // Ensure the filename and extension do not contain non-printable characters
//         for (int j = 0; j < 8; j++) if (filename[j] < 32) filename[j] = '\0';
//         for (int j = 0; j < 3; j++) if (extension[j] < 32) extension[j] = '\0';

//         // Display file or directory information
//         if (is_directory(entry)) {
//             printf("%-11s <DIR>            \n", filename);
//         } else if (is_file(entry)) {
//             printf("%-8s.%-3s    %10u\n", filename, extension, entry->fileSize);
//         }
//     }
// }

// // Function to read the contents of a directory in the FAT12 file system
// bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
//     printf("Reading FAT12 directory: %s\n", path);
//     int drive = 0;  // Drive A:

//     DirectoryEntry* root_directory = (DirectoryEntry*)malloc(sizeof(DirectoryEntry) * fat12.bootSector.rootEntryCount);
//     if (!root_directory) {
//         printf("Memory allocation error for root directory.\n");
//         return false;
//     }

//     // Read the Root Directory into a local buffer
//     if (!read_root_directory(drive, root_directory)) {
//         printf("Error reading the Root Directory.\n");
//         free(root_directory);
//         return false;
//     }

//     // Display the Root Directory
//     display_root_directory(root_directory, fat12.bootSector.rootEntryCount);
//     free(root_directory);
//     return true;
// }
