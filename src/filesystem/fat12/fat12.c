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

// Constants for floppy disk properties
#define SECTORS_PER_TRACK 18     // Number of sectors per track (typical for 1.44MB floppy disk)
#define NUMBER_OF_HEADS 2        // Number of disk heads (sides) for a double-sided floppy
#define MIN_CLUSTER_VALUE 0x002  // Minimum valid cluster value
#define MAX_CLUSTER_VALUE 0xFF8  // Maximum valid cluster value before end-of-chain


// Global structures and buffers
FAT12 fat12;
DirectoryEntry* entries = NULL;
DirectoryEntry* currentDir = NULL;
uint8_t* buffer = NULL;

uint8_t current_fdd_drive = 0;

// Function to calculate CHS from a logical sector number
void logical_to_chs(int logical_sector, int* track, int* head, int* sector) {
    *track = logical_sector / (SECTORS_PER_TRACK * NUMBER_OF_HEADS);
    *head = (logical_sector / SECTORS_PER_TRACK) % NUMBER_OF_HEADS;
    *sector = (logical_sector % SECTORS_PER_TRACK) + 1;  // Sectors are 1-based
}

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

    buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!buffer) {
        printf("Memory allocation failed for boot sector buffer.\n");
        return false;
    }

    if (!fdc_read_sector(drive, 0, 0, 1, buffer)) {
        printf("Error reading boot sector.\n");
        free(buffer);
        return false;
    }

    memcpy(&fat12->bootSector, buffer, sizeof(Fat12BootSector));
    free(buffer);

    if (fat12->bootSector.bootSectorSignature != 0xAA55) {
        printf("Invalid boot sector signature.\n");
        return false;
    }

    fat12->fatStart = fat12->bootSector.reservedSectors;
    fat12->rootDirStart = fat12->fatStart + (fat12->bootSector.fatCount * fat12->bootSector.sectorsPerFAT);
    fat12->dataStart = fat12->rootDirStart + (fat12->bootSector.rootEntryCount * ROOT_ENTRY_SIZE / SECTOR_SIZE);

    printf("FAT12 initialized: FAT Start Sector: %d, Root Directory Start Sector: %d, Data Region Start Sector: %d\n", 
           fat12->fatStart, fat12->rootDirStart, fat12->dataStart);
    return true;
}

// Initialize FAT12 and load root directory
bool fat12_init_fs(uint8_t drive) {
    current_fdd_drive = drive; // Set the current drive
    // Initialize FAT12 structure
    memset(&fat12, 0, sizeof(FAT12));

    if (!read_fat12(drive, &fat12)) {
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

// Read directory entries (root or subdirectory)
int fat12_read_dir_entries(DirectoryEntry* dir) {
    int entries_found = 0;
    entries = (DirectoryEntry*)malloc(MAX_ENTRIES * sizeof(DirectoryEntry));
    if (!entries) {
        printf("Memory allocation failed for directory entries.\n");
        return -1;
    }

    buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!buffer) {
        printf("Memory allocation failed for sector buffer.\n");
        free(entries);
        return -1;
    }

    if (dir == NULL) {
        printf("Reading root directory entries.\n");
        for (int i = 0; i < ROOT_DIR_SECTORS && entries_found < MAX_ENTRIES; i++) {
            int logical_sector = fat12.rootDirStart + i;
            int track, head, sector;
            logical_to_chs(logical_sector, &track, &head, &sector);

            if (!fdc_read_sector(current_fdd_drive, head, track, sector, buffer)) {
                printf("Error reading root directory sector %d.\n", i);
                free(entries);
                free(buffer);
                return -1;
            }

            memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
            entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
        }
    } else {
        // Read subdirectory starting from its cluster
        int cluster = dir->firstClusterLow;
        printf("Reading subdirectory. Start cluster: %d\n", cluster);

        while (cluster >= MIN_CLUSTER_VALUE && cluster < MAX_CLUSTER_VALUE && entries_found < MAX_ENTRIES) {
            int start_sector = fat12.dataStart + (cluster - 2) * fat12.bootSector.sectorsPerCluster;

            for (int i = 0; i < fat12.bootSector.sectorsPerCluster && entries_found < MAX_ENTRIES; i++) {
                int logical_sector = start_sector + i;
                int track, head, sector;
                logical_to_chs(logical_sector, &track, &head, &sector);

                if (!fdc_read_sector(current_fdd_drive, head, track, sector, buffer)) {
                    printf("Error reading subdirectory sector %d.\n", sector);
                    return -1;
                }

                // Check buffer content for debugging
                printf("Sector %d read successfully for subdirectory.\n", sector);

                memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
                entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
            }

            cluster = get_next_cluster(cluster); // Move to the next cluster in the chain
            printf("Next cluster: %d\n", cluster);
        }
    }

    printf("Entries found: %d\n", entries_found);

    free(buffer);
    return entries_found;
}

// Print directory entries in DOS-like format
void print_dir_entries(DirectoryEntry* dir, int entries_found) {
    printf(" Volume in drive A has no label\n");
    printf(" Directory of %s\n\n", dir == NULL ? "\\" : *dir->filename == 0 ? "\\" : (char*)dir->filename);
    printf("FILENAME   EXT    SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < entries_found; i++) {
        DirectoryEntry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & (0x10 | 0x20))) continue;

        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)entry->filename, 8);
        strncpy(extension, (const char*)entry->extension, 3);

        int day, month, year, hours, minutes, seconds;
        extract_date(entry->lastWriteDate, &day, &month, &year);
        extract_time(entry->lastWriteTime, &hours, &minutes, &seconds);

        if (entry->attributes & 0x10) {  // Directory
            printf("%-8s   %-3s   <DIR>    %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, day, month, year, hours, minutes, seconds);
        } else {  // File
            printf("%-8s   %-3s   %8u  %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, entry->fileSize, day, month, year, hours, minutes, seconds);
        }
    }
    printf("\n");
}

// Change to a new directory if it exists
bool fat12_change_directory(const char* relativePath) {
    int num_entries = fat12_read_dir_entries(currentDir);
    if (num_entries < 0) {
        printf("Failed to read directory.\n");
        return false;
    }

    for (int i = 0; i < num_entries; i++) {
        DirectoryEntry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & 0x10)) continue;  // Only directories

        char entry_name[9] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        entry_name[8] = '\0';  // Ensure null-termination

        char trimmed_name[9] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);

        if (strcmp(trimmed_name, relativePath) == 0) {
            // set trimmed_name to entry 
            str_trim_spaces(trimmed_name, (char*)entry->filename, 8);

            currentDir = entry;
            printf("Changed directory to %s\n", relativePath);
            return true;
        }
    }
    printf("Directory not found: %s\n", relativePath);
    return false;
}

// Open a file in the current directory
Fat12File* fat12_open_file(const char* filename, const char* mode) {
    int num_entries = fat12_read_dir_entries(currentDir);
    if (num_entries < 0) {
        printf("Failed to read directory contents.\n");
        return NULL;
    }

    DirectoryEntry* file_entry = NULL;
    for (int i = 0; i < num_entries; i++) {
        DirectoryEntry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x10) continue;  // Skip directories

        char entry_name[9] = {0};
        char entry_ext[4] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        strncpy(entry_ext, (const char*)entry->extension, 3);

        char trimmed_name[13] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);

        if (entry_ext[0] != ' ') {
            strncat(trimmed_name, ".", 1);
            strncat(trimmed_name, entry_ext, 3);
        }

        if (strcmp(trimmed_name, filename) == 0) {
            file_entry = entry;
            break;
        }
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return NULL;
    }

    Fat12File* file = malloc(sizeof(Fat12File));
    if (file == NULL) {
        printf("Failed to allocate memory for file structure.\n");
        return NULL;
    }

    file->base = malloc(file_entry->fileSize);
    if (file->base == NULL) {
        printf("Failed to allocate memory for file buffer.\n");
        free(file);
        return NULL;
    }

    file->ptr = file->base;
    file->startCluster = file_entry->firstClusterLow;
    file->size = file_entry->fileSize;
    file->position = 0;
    strncpy((char*)file->name, filename, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->mode = mode;

    printf("File opened: %s, Size: %d bytes\n", file->name, file->size);
    return file;
}

// Print the contents of a file
void print_file_content(Fat12File* file) {
    if (file == NULL) {
        printf("Invalid file handle.\n");
        return;
    }

    char* buffer = malloc(file->size + 1);
    if (buffer == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }
    memset(buffer, 0, file->size + 1);

    int bytesRead = fat12_read_file(file, buffer, file->size);
    if (bytesRead > 0) {
        printf("File contents:\n%s\n", buffer);
        hex_dump((unsigned char*)buffer, file->size);
    } else {
        printf("Failed to read file content.\n");
    }

    free(buffer);
}

// Read directory based on the specified path
bool fat12_read_dir(const char* path) {
    printf("-----Reading directory: %s-----\n", path ? path : "(current directory)");

    int num_entries = 0;
    if(currentDir == NULL) {
        // read root directory
        num_entries = fat12_read_dir_entries(NULL);
        if (num_entries < 0) {
            printf("Failed to load root directory.\n");
            return false;
        }
    }else {
        // read subdirectory
        num_entries = fat12_read_dir_entries(currentDir);
        if (num_entries < 0) {
            printf("Failed to load subdirectory.\n");
            return false;
        }
    }

    print_dir_entries(currentDir, num_entries);
    return true;
}

int fat12_read_file(Fat12File* file, void* buffer, size_t size) {
    if (file == NULL) {
        printf("Invalid file handle.\n");
        return 0;
    }

    if (file->position + size > file->size) {
        size = file->size - file->position; // Ensure we don't read beyond the end of the file
    }

    unsigned int bytes_read = 0;
    unsigned int currentCluster = file->startCluster;
    unsigned int clusterSize = SECTOR_SIZE * fat12.bootSector.sectorsPerCluster;

    // Adjust starting point if the file's position is not at the beginning
    unsigned int startOffset = file->position % clusterSize;
    unsigned char* sectorBuffer = malloc(SECTOR_SIZE);
    if (sectorBuffer == NULL) {
        printf("Memory allocation failed for sector buffer.\n");
        return 0;
    }

    while (bytes_read < size && currentCluster >= MIN_CLUSTER_VALUE && currentCluster < MAX_CLUSTER_VALUE) {
        // Calculate the logical sector for the current cluster
        unsigned int firstSectorOfCluster = fat12.dataStart + (currentCluster - 2) * fat12.bootSector.sectorsPerCluster;

        for (unsigned int i = 0; i < fat12.bootSector.sectorsPerCluster && bytes_read < size; i++) {
            int logical_sector = firstSectorOfCluster + i;
            int track = logical_sector / (SECTORS_PER_TRACK * NUMBER_OF_HEADS);
            int head = (logical_sector / SECTORS_PER_TRACK) % NUMBER_OF_HEADS;
            int sector = (logical_sector % SECTORS_PER_TRACK) + 1;  // Sectors are 1-based

            // Read the sector into the buffer
            if (!fdc_read_sector(current_fdd_drive, head, track, sector, sectorBuffer)) {
                printf("Error reading file sector at track %d, head %d, sector %d.\n", track, head, sector);
                free(sectorBuffer);
                return bytes_read; // Return bytes read so far on failure
            }

            // Determine the number of bytes to copy from the current sector
            unsigned int offset = (bytes_read == 0) ? startOffset : 0;
            unsigned int remaining = SECTOR_SIZE - offset;
            unsigned int bytes_to_read = (size - bytes_read < remaining) ? size - bytes_read : remaining;

            memcpy((unsigned char*)buffer + bytes_read, sectorBuffer + offset, bytes_to_read);
            bytes_read += bytes_to_read;
            file->position += bytes_to_read;

            if (bytes_read >= size) {
                break;
            }
        }

        // Get the next cluster from the FAT
        currentCluster = get_next_cluster(currentCluster);
    }

    // Null-terminate the buffer to ensure proper string output if applicable
    if (bytes_read < size) {
        ((char*)buffer)[bytes_read] = '\0';
    } else {
        ((char*)buffer)[size - 1] = '\0'; // Null-terminate at the end if size limit is reached
    }

    free(sectorBuffer);
    printf("Completed reading %d bytes from file %s into buffer.\n", bytes_read, file->name);

    return bytes_read;
}
