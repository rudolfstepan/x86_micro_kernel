#include "fat32.h"
#include "toolchain/stdio.h"

#define INVALID_SECTOR 0xFFFFFFFF


unsigned int readFileData(unsigned int startCluster, char* buffer, unsigned int bufferSize, unsigned int bytesToRead) {
    if (!buffer || bufferSize == 0 || bytesToRead == 0) {
        printf("Error: Invalid parameters for reading file data.\n");
        return 0;
    }

    unsigned int currentCluster = startCluster;
    unsigned int totalBytesRead = 0;

    while (totalBytesRead < bytesToRead) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);

        if (sectorNumber == INVALID_SECTOR) {
            printf("Error: Invalid sector number for cluster %u.\n", currentCluster);
            break;
        }

        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            unsigned int bytesRemaining = bytesToRead - totalBytesRead;
            unsigned int bytesToReadNow = (bytesRemaining < SECTOR_SIZE) ? bytesRemaining : SECTOR_SIZE;

            if (totalBytesRead + bytesToReadNow > bufferSize) {
                bytesToReadNow = bufferSize - totalBytesRead;
            }

            if (bytesToReadNow == 0) {
                break;
            }

            if (!ata_read_sector(ata_base_address, sectorNumber + i, buffer + totalBytesRead, ata_is_master)) {
                printf("Error: Failed to read sector %u.\n", sectorNumber + i);
                break;
            }

            totalBytesRead += bytesToReadNow;

            if (totalBytesRead >= bytesToRead) {
                break;
            }
        }

        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);

        if (is_end_of_cluster_chain(currentCluster) || currentCluster == INVALID_CLUSTER) {
            break;
        }
    }

    //printf("Read %u bytes from file.\n", totalBytesRead);
    return totalBytesRead;
}

int readFileDataToAddress(unsigned int startCluster, void* loadAddress, unsigned int fileSize) {
    if (!loadAddress || fileSize == 0) {
        printf("Error: Invalid parameters for reading file data to address.\n");
        return 0;
    }

    unsigned int currentCluster = startCluster;
    unsigned int bytesRead = 0;
    unsigned char* bufferPtr = (unsigned char*)loadAddress;

    while (bytesRead < fileSize) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);

        if (sectorNumber == INVALID_SECTOR) {
            printf("Error: Invalid sector number for cluster %u.\n", currentCluster);
            break;
        }

        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            if (!ata_read_sector(ata_base_address, sectorNumber + i, bufferPtr, ata_is_master)) {
                printf("Error: Failed to read sector %u.\n", sectorNumber + i);
                break;
            }

            bufferPtr += SECTOR_SIZE;
            bytesRead += SECTOR_SIZE;

            if (bytesRead >= fileSize) {
                break;
            }
        }

        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);

        if (is_end_of_cluster_chain(currentCluster) || currentCluster == INVALID_CLUSTER) {
            break;
        }
    }

    return bytesRead;
}

int fat32_load_file(const char* filename, void* loadAddress) {
    if (!filename || !loadAddress) {
        printf("Error: Invalid parameters for loading file.\n");
        return 0;
    }

    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (!entry) {
        printf("File %s not found.\n", filename);
        return 0;
    }

    unsigned int fileSize = entry->fileSize;
    unsigned int startCluster = read_start_cluster(entry);

    free(entry, sizeof(struct FAT32DirEntry));

    return readFileDataToAddress(startCluster, loadAddress, fileSize);
}

// Function to find a file in the current directory
struct FAT32DirEntry* findFileInDirectory(const char* filename) {
    if (!filename) {
        printf("Error: Invalid filename provided.\n");
        return NULL;
    }

    unsigned int sector = cluster_to_sector(&boot_sector, current_directory_cluster);
    size_t entriesPerSector = SECTOR_SIZE / sizeof(struct FAT32DirEntry);
    struct FAT32DirEntry* entries = malloc(SECTOR_SIZE * boot_sector.sectorsPerCluster);

    if (!entries) {
        printf("Error: Failed to allocate memory for directory entries.\n");
        return NULL;
    }

    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        if (!ata_read_sector(ata_base_address, sector + i, (void*)(entries + i * entriesPerSector), ata_is_master)) {
            printf("Error: Failed to read directory sector %u.\n", sector + i);
            free(entries, SECTOR_SIZE * boot_sector.sectorsPerCluster);
            return NULL;
        }
    }

    for (unsigned int j = 0; j < boot_sector.sectorsPerCluster * entriesPerSector; j++) {
        if (entries[j].name[0] == 0x00) {
            break;
        }

        if (entries[j].name[0] == 0xE5 || (entries[j].attr & 0x0F) == 0x0F) {
            continue;
        }

        char currentName[13];
        formatFilename(currentName, entries[j].name);

        if (strcmp(currentName, filename) == 0) {
            struct FAT32DirEntry* foundEntry = malloc(sizeof(struct FAT32DirEntry));

            if (!foundEntry) {
                printf("Error: Failed to allocate memory for directory entry.\n");
                free(entries, SECTOR_SIZE * boot_sector.sectorsPerCluster);
                return NULL;
            }

            memcpy(foundEntry, &entries[j], sizeof(struct FAT32DirEntry));
            free(entries, SECTOR_SIZE * boot_sector.sectorsPerCluster);
            return foundEntry;
        }
    }

    free(entries, SECTOR_SIZE * boot_sector.sectorsPerCluster);
    return NULL;
}


bool fat32_create_file(const char* filename) {
    // 1. Find a free cluster for the new file
    unsigned int newFileCluster = find_free_cluster(&boot_sector);
    if (newFileCluster == INVALID_CLUSTER) {
        printf("Failed to allocate a new cluster for the file.\n");
        return false;
    }
    // 2. Update the FAT for the new file cluster
    if (!mark_cluster_in_fat(&boot_sector, newFileCluster, FAT32_EOC_MAX)) {
        printf("Failed to update the FAT for the new file cluster.\n");
        return false;
    }
    // 3. Add a directory entry for the new file in the current directory
    if (!add_entry_to_directory(&boot_sector, current_directory_cluster, filename, newFileCluster, 0)) { // 0 for normal file attributes
        printf("Failed to add a directory entry for the new file.\n");
        return false;
    }
    return true;
}

bool fat32_delete_file(const char* filename) {
    // 1. Find the directory entry for the file to delete
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return false;
    }
    // 2. Free the file's cluster chain in the FAT
    if (!free_cluster_chain(&boot_sector, read_start_cluster(entry))) {
        printf("Failed to free the file's cluster chain.\n");
        return false;
    }
    // 3. Remove the directory entry from the parent directory
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster, entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        return false;
    }
    return true;
}

// Function to open a file and return a pointer to the file data
FILE* fat32_open_file(const char* filename, const char* mode) {
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return NULL;
    }

    // Read file details
    unsigned int startCluster = read_start_cluster(entry);
    size_t fileSize = (size_t)entry->fileSize;

    // Allocate memory for the FILE structure
    FILE* file = (FILE*)malloc(sizeof(FILE));
    if (file == NULL) {
        printf("Not enough memory for file structure.\n");
        return NULL;
    }

    printf("Allocated file handler with %u bytes.\n", sizeof(FILE));

    if (strcmp(mode, "r+") == 0) { // Fully load into memory

        // Allocate memory for the file content
        file->base = (unsigned char*)malloc(fileSize);
        if (file->base == NULL) {
            printf("Not enough memory for file content.\n");
            free(file, sizeof(FILE));  // Free the FILE structure before returning
            return NULL;
        }

        printf("Allocated file content with %u bytes.\n", fileSize);

        size_t totalBytesRead = 0;
        char chunkBuffer[512];

        while (totalBytesRead < fileSize) {
            size_t bytesToRead = (fileSize - totalBytesRead < 512) ? (fileSize - totalBytesRead) : 512;
            if (readFileData(startCluster, chunkBuffer, sizeof(chunkBuffer), bytesToRead) != bytesToRead) {
                printf("Error reading file data.\n");
                free(file->base, fileSize);  // Free the file content
                free(file, sizeof(FILE));  // Free the FILE structure before returning
                return NULL;
            }
            memcpy(file->base + totalBytesRead, chunkBuffer, bytesToRead);
            totalBytesRead += bytesToRead;
        }
    }

    // Initialize FILE structure fields
    file->position = 0;
    file->size = fileSize;
    file->startCluster = startCluster;
    file->mode = strdup(mode);  // Duplicate the mode string
    file->name = strdup(filename);  // Duplicate the filename string

    printf("Allocated file handler with %u bytes and loaded content.\n", sizeof(FILE) + fileSize);

    return file;
}

// read file
int fat32_read_file(FILE* file, char* buffer, unsigned int buffer_size, unsigned int bytesToRead) {
    if (strcmp(file->mode, "w") == 0) {
        printf("Error: File is not open for reading.\n");
        return 0;
    }

    if (file->position + bytesToRead > file->size) {
        bytesToRead = file->size - file->position;
    }

    unsigned int bytesRead = readFileData(file->startCluster, buffer, buffer_size, bytesToRead);
    file->position += bytesRead;  // Update the file pointer
    return bytesRead;
}
