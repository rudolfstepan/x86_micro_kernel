#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

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
#define INVALID_CLUSTER 0xFFF // Typically used for FAT12; adjust based on your system


// Global structures and buffers
FAT12* fat12;
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
    fat12 = (FAT12*)malloc(sizeof(FAT12));
    if (!fat12) {
        printf("Memory allocation failed for FAT12 structure.\n");
        return false;
    }
    //memset(&fat12, 0, sizeof(FAT12));

    if (!read_fat12(drive, fat12)) {
        printf("Failed to initialize FAT12.\n");
        return false;
    }
    return true;
}

// Function to calculate and fetch the next cluster in FAT12
int get_next_cluster(int currentCluster) {
    int offset = (currentCluster * 3) / 2;
    uint16_t next_cluster = (offset % 2 == 0)
        ? (fat12->fat[offset] | (fat12->fat[offset + 1] << 8)) & 0x0FFF
        : ((fat12->fat[offset] >> 4) | (fat12->fat[offset + 1] << 4)) & 0x0FFF;

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

    buffer = (uint8_t*)malloc(SECTOR_SIZE * ROOT_DIR_SECTORS);  // Allocate buffer for multiple sectors
    if (!buffer) {
        printf("Memory allocation failed for sector buffer.\n");
        free(entries);
        return -1;
    }

    if (dir == NULL) {
        printf("Reading root directory entries.\n");

        int logical_sector = fat12->rootDirStart;
        int track, head, sector;
        logical_to_chs(logical_sector, &track, &head, &sector);

        // Read multiple sectors at once
        if (!fdc_read_sectors(current_fdd_drive, head, track, sector, ROOT_DIR_SECTORS, buffer)) {
            printf("Error reading root directory sectors.\n");
            free(entries);
            free(buffer);
            return -1;
        }

        // Copy entries from the buffer
        for (int i = 0; i < ROOT_DIR_SECTORS && entries_found < MAX_ENTRIES; i++) {
            memcpy(&entries[entries_found], buffer + (i * SECTOR_SIZE), SECTOR_SIZE);
            entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
        }
    } else {
        // Read subdirectory starting from its cluster
        int cluster = dir->firstClusterLow;
        printf("Reading subdirectory. Start cluster: %d\n", cluster);

        while (cluster >= MIN_CLUSTER_VALUE && cluster < MAX_CLUSTER_VALUE && entries_found < MAX_ENTRIES) {
            int start_sector = fat12->dataStart + (cluster - 2) * fat12->bootSector.sectorsPerCluster;
            int track, head, sector;
            logical_to_chs(start_sector, &track, &head, &sector);

            // Read multiple sectors for the cluster
            if (!fdc_read_sectors(current_fdd_drive, head, track, sector, fat12->bootSector.sectorsPerCluster, buffer)) {
                printf("Error reading subdirectory sectors starting from sector %d.\n", sector);
                free(entries);
                free(buffer);
                return -1;
            }

            // Copy entries from the buffer
            for (int i = 0; i < fat12->bootSector.sectorsPerCluster && entries_found < MAX_ENTRIES; i++) {
                memcpy(&entries[entries_found], buffer + (i * SECTOR_SIZE), SECTOR_SIZE);
                entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
            }

            cluster = get_next_cluster(cluster);  // Move to the next cluster in the chain
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

    Fat12File* file = (Fat12File*)malloc(sizeof(Fat12File));
    if (file == NULL) {
        printf("Failed to allocate memory for file structure.\n");
        return NULL;
    }

    file->base = (unsigned char*)malloc(file_entry->fileSize);
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

    // Calculate the size of the buffer based on the size of the file
    size_t bufferSize = file->size; // Use the file size as the buffer size directly

    // Allocate the buffer
    char* buffer = (char*)malloc(sizeof(char) * bufferSize);

    if (buffer == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }
    memset(buffer, 0, bufferSize);

    int bytesRead = fat12_read_file(file, buffer, bufferSize, file->size);
    if (bytesRead > 0) {
        printf("File contents:\n%s\n", buffer);
        hex_dump((unsigned char*)buffer, bufferSize);
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

int fat12_read_file(Fat12File* file, void* buffer, unsigned int buffer_size, unsigned int bytesToRead) {
    if (file == NULL || buffer == NULL) {
        printf("Invalid file handle or buffer.\n");
        return 0;
    }

    // Ensure we don't read beyond the end of the file or buffer
    if (file->position + bytesToRead > file->size) {
        bytesToRead = file->size - file->position;
    }
    if (bytesToRead > buffer_size) {
        bytesToRead = buffer_size;
    }

    unsigned int bytes_read = 0;
    unsigned int currentCluster = file->startCluster;
    unsigned int clusterSize = SECTOR_SIZE * fat12->bootSector.sectorsPerCluster;

    // Calculate initial offset within the first cluster if the position is not at the start
    unsigned int startOffset = file->position % clusterSize;

    unsigned char* sectorBuffer = (unsigned char*)malloc(SECTOR_SIZE);
    if (sectorBuffer == NULL) {
        printf("Memory allocation failed for sector buffer.\n");
        return 0;
    }

    // Read loop
    while (bytes_read < bytesToRead && currentCluster >= MIN_CLUSTER_VALUE && currentCluster < MAX_CLUSTER_VALUE) {
        // Calculate the first sector of the current cluster
        unsigned int firstSectorOfCluster = fat12->dataStart + (currentCluster - 2) * fat12->bootSector.sectorsPerCluster;

        // Read each sector in the cluster
        for (unsigned int i = 0; i < fat12->bootSector.sectorsPerCluster && bytes_read < bytesToRead; i++) {
            unsigned int logical_sector = firstSectorOfCluster + i;
            int track = logical_sector / (SECTORS_PER_TRACK * NUMBER_OF_HEADS);
            int head = (logical_sector / SECTORS_PER_TRACK) % NUMBER_OF_HEADS;
            int sector = (logical_sector % SECTORS_PER_TRACK) + 1;  // Sectors are 1-based

            // Read the sector
            if (!fdc_read_sector(current_fdd_drive, head, track, sector, sectorBuffer)) {
                printf("Error reading file sector at track %d, head %d, sector %d.\n", track, head, sector);
                free(sectorBuffer);
                return bytes_read; // Return bytes read so far on failure
            }

            // Calculate how many bytes to copy from the sector
            unsigned int offset = (bytes_read == 0) ? startOffset : 0;
            unsigned int remaining = SECTOR_SIZE - offset;
            unsigned int bytes_to_copy = (bytesToRead - bytes_read < remaining) ? (bytesToRead - bytes_read) : remaining;

            // Copy the bytes from the sector buffer to the main buffer
            memcpy((unsigned char*)buffer + bytes_read, sectorBuffer + offset, bytes_to_copy);
            bytes_read += bytes_to_copy;
            file->position += bytes_to_copy;

            if (bytes_read >= bytesToRead) {
                break;
            }
        }

        // Move to the next cluster in the chain
        currentCluster = get_next_cluster(currentCluster);
        if (currentCluster == INVALID_CLUSTER) {
            printf("Invalid cluster detected.\n");
            break;
        }
    }

    // Null-terminate the buffer if applicable (only for text data)
    if (bytes_read < buffer_size) {
        ((char*)buffer)[bytes_read] = '\0';
    }

    free(sectorBuffer);
    printf("Completed reading %u bytes from file %s into buffer.\n", bytes_read, file->name);

    return bytes_read;
}
