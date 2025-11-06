#include "fat32.h"
#include "lib/libc/stdio.h"

// Function to read a file's data into a buffer
// void readFileData(unsigned int startCluster, char* buffer, unsigned int size) {
//     unsigned int currentCluster = startCluster;
//     unsigned int bytesRead = 0;
//     while (bytesRead < size) {
//         unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);
//         // Read each sector in the current cluster
//         for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
//             ata_read_sector(ata_base_address, sectorNumber + i, buffer + bytesRead, ata_is_master);
//             bytesRead += SECTOR_SIZE;
//             if (bytesRead >= size) {
//                 break;  // Stop if we have read the required size
//             }
//         }
//         // Get the next cluster in the chain
//         currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);
//         // Check if we have reached the end of the file
//         if (isEndOfClusterChain(currentCluster)) {
//             break;
//         }
//     }
// }

unsigned int readFileData(unsigned int startCluster, char* buffer, unsigned int bufferSize, unsigned int bytesToRead) {
    if (buffer == NULL || bufferSize == 0 || bytesToRead == 0) {
        // Invalid parameters; return 0 to indicate no data read
        return 0;
    }

    unsigned int currentCluster = startCluster;
    unsigned int totalBytesRead = 0;

    while (totalBytesRead < bytesToRead) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);

        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            // Calculate the number of bytes to read in this iteration
            unsigned int bytesRemaining = bytesToRead - totalBytesRead;
            unsigned int bytesToReadNow = (bytesRemaining < SECTOR_SIZE) ? bytesRemaining : SECTOR_SIZE;

            // Ensure we don't read past the end of the buffer
            if (totalBytesRead + bytesToReadNow > bufferSize) {
                bytesToReadNow = bufferSize - totalBytesRead;
            }

            // Read data into the buffer
            ata_read_sector(ata_base_address, sectorNumber + i, buffer + totalBytesRead, ata_is_master);
            totalBytesRead += bytesToReadNow;

            // Break the loop if we have read the requested number of bytes
            if (totalBytesRead >= bytesToRead) {
                break;
            }
        }

        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);

        // Check if we have reached the end of the file or if an invalid cluster is encountered
        if (is_end_of_cluster_chain(currentCluster) || currentCluster == INVALID_CLUSTER) {
            break;
        }
    }

    // Return the total number of bytes read
    return totalBytesRead;
}

int readFileDataToAddress(unsigned int startCluster, void* loadAddress, unsigned int fileSize) {
    unsigned int currentCluster = startCluster;
    unsigned int bytesRead = 0;
    unsigned char* bufferPtr = (unsigned char*)loadAddress;
    while (bytesRead < fileSize) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            ata_read_sector(ata_base_address, sectorNumber + i, bufferPtr, ata_is_master);
            bufferPtr += SECTOR_SIZE;
            bytesRead += SECTOR_SIZE;

            if (bytesRead >= fileSize) {
                break; // Stop if we have read the entire file
            }
        }
        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);
        // Check if we have reached the end of the file
        if (is_end_of_cluster_chain(currentCluster)) {
            break;
        }
    }
    return fileSize;
}

int fat32_load_file(const char* filename, void* loadAddress) {
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File %s not found for loading into buffer.\n", filename);
        return 0; // we return 0 if the file was not found which is the size of the file
    }
    unsigned int fileSize = entry->fileSize;
    unsigned int startCluster = read_start_cluster(entry);
    // Assuming readFileData is modified to take a pointer to the load address
    return readFileDataToAddress(startCluster, loadAddress, fileSize);
}

// void openAndLoadFile(const char* filename) {
//     struct FAT32DirEntry* entry = findFileInDirectory(filename);
//     if (entry == NULL) {
//         printf("File not found.\n");
//         return;
//     }
//     unsigned int startCluster = read_start_cluster(entry);
//     int fileSize = entry->fileSize;
//     char* buffer = malloc(fileSize);
//     if (buffer == NULL) {
//         printf("Not enough memory.\n");
//         return;
//     }
//     readFileData(startCluster, buffer, sizeof(buffer), fileSize);

//     // Process the file data in bufferb
//     // for(int i = 0; i < fileSize; i++){
//     //     printf("%c", buffer[i]);
//     // }
//     free(entry); // Free the memory after use
//     free(buffer); // Free the memory after use
// }

// Function to find a file in the current directory
struct FAT32DirEntry* findFileInDirectory(const char* filename) {
    unsigned int sector = cluster_to_sector(&boot_sector, current_directory_cluster);
    struct FAT32DirEntry* entries = (struct FAT32DirEntry*)malloc(SECTOR_SIZE * boot_sector.sectorsPerCluster / sizeof(struct FAT32DirEntry));

    if (entries == NULL) {
        // Handle memory allocation failure
        printf("Error: Failed to allocate memory for directory entries.\n");
        return NULL;
    }
    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        if (!ata_read_sector(ata_base_address, sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))], ata_is_master)) {
            // Handle read error
            free(entries);
            return NULL;
        }
    }

    for (unsigned int j = 0; j < boot_sector.sectorsPerCluster * (SECTOR_SIZE / sizeof(struct FAT32DirEntry)); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            printf("  End of directory at entry %u\n", j);
            break;
        }

        if (entries[j].name[0] == 0xE5 || (entries[j].attr & 0x0F) == 0x0F) { // Deleted or LFN entry
            continue;
        }

        // Debug: show the raw FAT32 name
        char debugName[12];
        for (int k = 0; k < 11; k++) {
            debugName[k] = entries[j].name[k];
        }
        debugName[11] = '\0';
        printf("  Comparing FAT32 name '%.11s' with '%s'\n", debugName, filename);
        
        // Use the proper compare_names function that handles FAT32 8.3 format correctly
        if (compare_names((const char*)entries[j].name, filename) == 0) {
            printf("  MATCH FOUND!\n");
            struct FAT32DirEntry* foundEntry = (struct FAT32DirEntry*)malloc(sizeof(struct FAT32DirEntry));
            if (foundEntry == NULL) {
                // Handle memory allocation failure
                printf("Error: Failed to allocate memory for directory entry.\n");
                free(entries);
                return NULL;
            }

            memcpy(foundEntry, &entries[j], sizeof(struct FAT32DirEntry));
            free(entries); // Free the allocated memory
            return foundEntry; // File found
        }
    }

    printf("  File '%s' not found in directory\n", filename);
    free(entries); // Free the allocated memory
    return NULL; // File not found
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

    unsigned int startCluster = read_start_cluster(entry);
    int fileSize = entry->fileSize;

    FILE* file = (FILE*)malloc(sizeof(FILE));
    if (file == NULL) {
        printf("Not enough memory.\n");
        return NULL;
    }

    file->position = 0;
    file->size = fileSize;
    file->ptr = (unsigned char*)malloc(fileSize);
    file->mode = mode;
    file->name = filename;
    file->startCluster = startCluster;

    return file;
}

// read file
int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytesToRead) {
    if (strcmp(file->mode, "w") == 0) {
        printf("Error: File is not open for reading.\n");
        return 0;
    }

    if (file->position + bytesToRead > file->size) {
        bytesToRead = file->size - file->position;
    }

    return readFileData(file->startCluster, (char*)buffer, buffer_size, bytesToRead);
}
