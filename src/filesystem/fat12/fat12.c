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
#define DRIVE_NUMBER 0           // Drive number (0 for primary drive)


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


/**
 * Converts a logical sector number to CHS (Cylinder-Head-Sector) format.
 *
 * @param logicalSector The logical sector number to convert.
 * @param sectorsPerTrack The number of sectors per track on the disk.
 * @param numberOfHeads The number of heads (sides) on the disk.
 * @param track Pointer to an integer where the calculated track (cylinder) will be stored.
 * @param head Pointer to an integer where the calculated head (side) will be stored.
 * @param sector Pointer to an integer where the calculated sector (1-based) will be stored.
 *
 * This function calculates the CHS values based on the given logical sector number.
 * The sector number in CHS is 1-based, so 1 is added after calculating the sector value.
 */
void logical_to_chs(int logicalSector, int sectorsPerTrack, int numberOfHeads, int* track, int* head, int* sector) {
    // Calculate the track (cylinder) by dividing the logical sector by the total number of sectors per cylinder
    // Total sectors per cylinder = sectorsPerTrack * numberOfHeads
    *track = logicalSector / (sectorsPerTrack * numberOfHeads);

    // Calculate the head (side) by dividing the logical sector by the number of sectors per track
    // and taking the remainder when divided by the number of heads
    *head = (logicalSector / sectorsPerTrack) % numberOfHeads;

    // Calculate the sector within the track by taking the remainder of the logical sector divided by the number of sectors per track
    // Add 1 because sector numbers in CHS format are typically 1-based
    *sector = (logicalSector % sectorsPerTrack) + 1;
}

int entries_found;

int fat12_read_dir_entries(DirectoryEntry* dir) {
    entries_found = 0;
    // Handle root directory
    if (dir == NULL) {
        for (int i = 0; i < ROOT_DIR_SECTORS && entries_found < MAX_ENTRIES; i++) {
            int logical_sector = fat12.rootDirStart + i;
            // int track = logical_sector / (SECTORS_PER_TRACK * NUMBER_OF_HEADS);
            // int head = (logical_sector / SECTORS_PER_TRACK) % NUMBER_OF_HEADS;
            // int sector = (logical_sector % SECTORS_PER_TRACK) + 1;  // Sectors are 1-based
            int track, head, sector;
            // Use the new method to convert logical sector to CHS
            logical_to_chs(logical_sector, SECTORS_PER_TRACK, NUMBER_OF_HEADS, &track, &head, &sector);
            if (!fdc_read_sector(DRIVE_NUMBER, head, track, sector, buffer)) {
                printf("Error reading root directory sector %d.\n", i);
                return -1;
            }

            memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
            entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
        }
    } else {
        // Handle subdirectory by traversing clusters
        int cluster = dir->firstClusterLow;

        while (cluster >= MIN_CLUSTER_VALUE && cluster < MAX_CLUSTER_VALUE && entries_found < MAX_ENTRIES) {
            int start_sector = fat12.dataStart + (cluster - 2) * fat12.bootSector.sectorsPerCluster;

            for (int i = 0; i < fat12.bootSector.sectorsPerCluster && entries_found < MAX_ENTRIES; i++) {
                int logical_sector = start_sector + i;
                // int track = logical_sector / (SECTORS_PER_TRACK * NUMBER_OF_HEADS);
                // int head = (logical_sector / SECTORS_PER_TRACK) % NUMBER_OF_HEADS;
                // int sector = (logical_sector % SECTORS_PER_TRACK) + 1;  // Sectors are 1-based
                int track, head, sector;
                // Use the new method to convert logical sector to CHS
                logical_to_chs(logical_sector, SECTORS_PER_TRACK, NUMBER_OF_HEADS, &track, &head, &sector);

                if (!fdc_read_sector(DRIVE_NUMBER, head, track, sector, buffer)) {
                    printf("Error reading directory sector %d.\n", sector);
                    return -1;
                }

                memcpy(&entries[entries_found], buffer, SECTOR_SIZE);
                entries_found += SECTOR_SIZE / ROOT_ENTRY_SIZE;
            }

            cluster = get_next_cluster(cluster);  // Move to the next cluster in the chain
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

void print_dir_entries(DirectoryEntry* dir){
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
    printf("\n");
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
        print_dir_entries(currentDir);
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

    print_dir_entries(currentDir);
    return true;
}

Fat12File* fat12_open_file(const char* filename, const char* mode) {
    // Find the file in the current directory
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
        if (entry->attributes & 0x10) continue; // Skip directories

        char entry_name[9] = {0};
        char entry_ext[4] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        strncpy(entry_ext, (const char*)entry->extension, 3);

        // Normalize the filename
        char trimmed_name[13] = {0}; // Buffer for 8.3 format (8 + 1 + 3 + null terminator)
        str_trim_spaces(entry_name, trimmed_name, 8);

        // Append extension if present
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

    // Allocate memory for the file structure
    Fat12File* file = malloc(sizeof(Fat12File));
    if (file == NULL) {
        printf("Failed to allocate memory for file structure.\n");
        return NULL;
    }

    // Initialize the file structure
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
    strncpy((char*)file->name, filename, sizeof(file->name) - 1); // Copy the filename safely
    file->name[sizeof(file->name) - 1] = '\0'; // Ensure null-termination
    file->mode = mode;

    printf("File opened: %s, Size: %d bytes\n", file->name, file->size);
    return file;
}

int fat12_read_file(Fat12File* file, void* buffer, size_t size) {
    if (file == NULL) {
        printf("File not found.\n");
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
    unsigned char sectorBuffer[SECTOR_SIZE];

    while (bytes_read < size && currentCluster >= 0x002 && currentCluster < 0xFF8) {
        // Calculate the logical sector for the current cluster
        unsigned int firstSectorOfCluster = fat12.dataStart + (currentCluster - 2) * fat12.bootSector.sectorsPerCluster;

        for (unsigned int i = 0; i < fat12.bootSector.sectorsPerCluster && bytes_read < size; i++) {
            int logical_sector = firstSectorOfCluster + i;
            int track, head, sector;

            // Use the new method to convert logical sector to CHS
            logical_to_chs(logical_sector, SECTORS_PER_TRACK, NUMBER_OF_HEADS, &track, &head, &sector);

            // Read the sector into the buffer
            if (!fdc_read_sector(0, head, track, sector, sectorBuffer)) {
                printf("Error reading file sector at track %d, head %d, sector %d.\n", track, head, sector);
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

    printf("Completed reading %d bytes from file %s into buffer.\n", bytes_read, file->name);

    return bytes_read;
}

void print_file_content(Fat12File* file) {
    if (file == NULL) {
        printf("Invalid file handle.\n");
        return;
    }

    char* buffer = malloc(file->size + 1); // Allocate buffer with space for null terminator
    // Ensure buffer allocation was successful
    memset(buffer, 0, file->size + 1);
    if (buffer == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }

    int bytesRead = fat12_read_file(file, buffer, file->size);
    if (bytesRead > 0) {
        printf("File contents:\n%s\n", buffer);

        // Print the file content in hex format
        hex_dump((unsigned char*)buffer, file->size);

    } else {
        printf("Failed to read file content.\n");
    }

    free(buffer);
}

