#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "drivers/keyboard/keyboard.h"

// Constants
#define MAX_PATH_LENGTH 256
#define MAX_ENTRIES 224
#define ROOT_ENTRY_SIZE 32
#define SECTOR_SIZE 512
#define ROOT_DIR_SECTORS 14

// Global structures and buffers
FAT12 fat12;
DirectoryEntry entries[MAX_ENTRIES];
DirectoryEntry* currentDir = NULL;
uint8_t buffer[SECTOR_SIZE];

// Valid filename check
bool is_valid_filename(const char* filename, size_t length) {
    const char* valid_chars = "!#$%&'()-@^_`{}~";
    for (size_t i = 0; i < length; i++) {
        if (!isalnum(filename[i]) && !strchr(valid_chars, filename[i])) {
            return false;
        }
    }
    return true;
}

// Read the FAT table and initialize FAT12 structure
int read_fat12(uint8_t drive, FAT12* fat12) {
    if (fat12->bootSector.bootSectorSignature == 0xAA55) {
        printf("FAT12 already initialized.\n");
        return true;
    }

    if (!fdc_read_sector(drive, 0, 0, 1, buffer)) {
        printf("Error reading boot sector.\n");
        return false;
    }

    memcpy(&fat12->bootSector, buffer, sizeof(Fat12BootSector));
    if (fat12->bootSector.bootSectorSignature != 0xAA55) {
        printf("Invalid boot sector signature.\n");
        return false;
    }

    fat12->fatStart = fat12->bootSector.reservedSectors;
    fat12->rootDirStart = fat12->fatStart + (fat12->bootSector.fatCount * fat12->bootSector.sectorsPerFAT);
    fat12->dataStart = fat12->rootDirStart + (fat12->bootSector.rootEntryCount * ROOT_ENTRY_SIZE / SECTOR_SIZE);

    printf("FAT12 initialized: FAT Start Sector: %d, Root Directory Start Sector: %d, Data Region Start Sector: %d\n", fat12->fatStart, fat12->rootDirStart, fat12->dataStart);
    return true;
}

// Initialize FAT12 and load root directory
bool fat12_init_fs() {
    if (!read_fat12(0, &fat12)) {
        printf("Failed to initialize FAT12.\n");
        return false;
    }
    return true;
}

// Function to calculate and fetch the next cluster in FAT12
int get_next_cluster(int currentCluster) {
    int offset = (currentCluster * 3) / 2;
    uint16_t next_cluster = (offset % 2 == 0)
        ? (fat12.fat[offset] | (fat12.fat[offset + 1] << 8)) & 0x0FFF
        : ((fat12.fat[offset] >> 4) | (fat12.fat[offset + 1] << 4)) & 0x0FFF;

    return next_cluster >= 0xFF8 ? -1 : next_cluster;
}

// Function to extract date from FAT12 format
void extract_date(uint16_t fat_date, int* day, int* month, int* year) {
    *day = fat_date & 0x1F;
    *month = (fat_date >> 5) & 0x0F;
    *year = ((fat_date >> 9) & 0x7F) + 1980;
}

// Function to extract time from FAT12 format
void extract_time(uint16_t fat_time, int* hours, int* minutes, int* seconds) {
    *seconds = (fat_time & 0x1F) * 2;
    *minutes = (fat_time >> 5) & 0x3F;
    *hours = (fat_time >> 11) & 0x1F;
}

int fat12_read_dir_entries(DirectoryEntry* dir) {
    int entries_found = 0;

    // Handle root directory
    if (dir == NULL) {
        for (int i = 0; i < ROOT_DIR_SECTORS && entries_found < MAX_ENTRIES; i++) {
            int logical_sector = fat12.rootDirStart + i;
            int track = logical_sector / (18 * 2);
            int head = (logical_sector / 18) % 2;
            int sector = (logical_sector % 18) + 1;

            if (!fdc_read_sector(0, head, track, sector, buffer)) {
                printf("Error reading root directory sector %d.\n", i);
                return -1;
            }

            memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
            entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
        }
    } else {
        // Handle subdirectory by traversing clusters
        int cluster = dir->firstClusterLow;

        while (cluster >= 0x002 && cluster < 0xFF8 && entries_found < MAX_ENTRIES) {
            int start_sector = fat12.dataStart + (cluster - 2) * fat12.bootSector.sectorsPerCluster;

            for (int i = 0; i < fat12.bootSector.sectorsPerCluster && entries_found < MAX_ENTRIES; i++) {
                int logical_sector = start_sector + i;
                int track = logical_sector / (18 * 2);
                int head = (logical_sector / 18) % 2;
                int sector = (logical_sector % 18) + 1;

                if (!fdc_read_sector(0, head, track, sector, buffer)) {
                    printf("Error reading directory sector %d.\n", sector);
                    return -1;
                }

                memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
                entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
            }

            cluster = get_next_cluster(cluster);  // Move to the next cluster in the chain
        }
    }

    // Display directory contents in DOS-like format
    printf(" Volume in drive A has no label\n");
    printf(" Directory of %s\n\n", dir == NULL ? "\\" : "");
    printf("FILENAME   EXT    SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < entries_found; i++) {
        DirectoryEntry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & (0x10 | 0x20))) continue;

        // Format filename and extension
        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)entry->filename, 8);
        strncpy(extension, (const char*)entry->extension, 3);

        // Extract date and time
        int day, month, year, hours, minutes, seconds;
        extract_date(entry->lastWriteDate, &day, &month, &year);
        extract_time(entry->lastWriteTime, &hours, &minutes, &seconds);

        // Print in DOS-like format
        if (entry->attributes & 0x10) {  // Directory
            printf("%-8s   %-3s   <DIR>    %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, day, month, year, hours, minutes, seconds);
        } else {  // File
            printf("%-8s   %-3s   %8u  %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, entry->fileSize, day, month, year, hours, minutes, seconds);
        }
    }

    return entries_found;
}

// Change to a new directory if it exists
bool fat12_change_directory(const char* relativePath) {
    int num_entries = fat12_read_dir_entries(currentDir);
    if (num_entries < 0) {
        printf("Failed to specified read directory.\n");
        return false;
    }

    for (int i = 0; i < num_entries; i++) {
        DirectoryEntry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & (0x10 | 0x20))) continue;

        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)entry->filename, 8);
        strncpy(extension, (const char*)entry->extension, 3);

        // normalize the filename
        char trimmed_name[9] = {0};
        str_trim_spaces(filename, trimmed_name, 8);

        if (strcmp(trimmed_name, relativePath) == 0) {
            currentDir = entry;
            printf("Changed directory to %s\n", relativePath);
            return true;
        }
    }
    printf("Directory not found: %s\n", relativePath);
    return false;
}

// Read directory based on the specified path
bool fat12_read_dir(const char* path) {
    printf("-----Reading directory: %s-----\n", path ? path : "(current directory)");

    // If path is NULL or an empty string, list the current directory
    if (path == NULL || path[0] == '\0') {
        int num_entries = fat12_read_dir_entries(currentDir);
        if (num_entries < 0) {
            printf("Failed to read current directory contents.\n");
            return false;
        }
        return true;
    }

    char path_copy[MAX_PATH_LENGTH];
    strncpy(path_copy, path, MAX_PATH_LENGTH);
    path_copy[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination

    // Determine if path is absolute
    bool is_absolute_path = (path[0] == '/');
    DirectoryEntry* previousDir = currentDir; // Save the original directory

    // If the path is absolute, start from the root directory
    if (is_absolute_path) {
        currentDir = NULL;  // Set to root

        // Load the root directory explicitly
        int root_entries = fat12_read_dir_entries(NULL);
        if (root_entries < 0) {
            printf("Failed to load root directory.\n");
            currentDir = previousDir; // Revert to the original directory
            return false;
        }
    }

    // Split the path into components (directories)
    char* token = strtok(path_copy, "/");

    // Traverse each directory in the path
    while (token != NULL) {
        printf("Token: %s\n", token);
        if (!fat12_change_directory(token)) {
            printf("Directory not found: %s\n", token);
            currentDir = previousDir; // Revert to the original directory if path traversal fails
            return false;
        }
        token = strtok(NULL, "/");
    }

    // Now that we have changed to the specified path, read the directory entries
    int num_entries = fat12_read_dir_entries(currentDir);
    if (num_entries < 0) {
        printf("Failed to read directory contents.\n");
        currentDir = previousDir; // Revert if reading fails
        return false;
    }

    return true;
}
