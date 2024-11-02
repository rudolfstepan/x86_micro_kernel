#include "fat32.h"
#include "toolchain/stdio.h"

// Function to read a file's data into a buffer
void readFileData(unsigned int startCluster, char* buffer, unsigned int size) {
    unsigned int currentCluster = startCluster;
    unsigned int bytesRead = 0;
    while (bytesRead < size) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            ata_read_sector(sectorNumber + i, buffer + bytesRead);
            bytesRead += SECTOR_SIZE;
            if (bytesRead >= size) {
                break;  // Stop if we have read the required size
            }
        }
        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);
        // Check if we have reached the end of the file
        if (isEndOfClusterChain(currentCluster)) {
            break;
        }
    }
}

int openAndLoadFileToBuffer(const char* filename, void* loadAddress) {
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        //printf("File %s not found for loading into buffer.\n", filename);
        return 0; // we return 0 if the file was not found which is the size of the file
    }
    unsigned int fileSize = entry->fileSize;
    unsigned int startCluster = readStartCluster(entry);
    // Assuming readFileData is modified to take a pointer to the load address
    return readFileDataToAddress(startCluster, loadAddress, fileSize);
}

int readFileDataToAddress(unsigned int startCluster, void* loadAddress, unsigned int fileSize) {
    unsigned int currentCluster = startCluster;
    unsigned int bytesRead = 0;
    unsigned char* bufferPtr = (unsigned char*)loadAddress;
    while (bytesRead < fileSize) {
        unsigned int sectorNumber = cluster_to_sector(&boot_sector, currentCluster);
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
            ata_read_sector(sectorNumber + i, bufferPtr);
            bufferPtr += SECTOR_SIZE;
            bytesRead += SECTOR_SIZE;

            if (bytesRead >= fileSize) {
                break; // Stop if we have read the entire file
            }
        }
        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&boot_sector, currentCluster);
        // Check if we have reached the end of the file
        if (isEndOfClusterChain(currentCluster)) {
            break;
        }
    }
    return fileSize;
}

void openAndLoadFile(const char* filename) {
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return;
    }
    unsigned int startCluster = readStartCluster(entry);
    int fileSize = entry->fileSize;
    char* buffer = malloc(fileSize);
    if (buffer == NULL) {
        printf("Not enough memory.\n");
        return;
    }
    readFileData(startCluster, buffer, fileSize);

    // Process the file data in buffer
    // for(int i = 0; i < fileSize; i++){
    //     printf("%c", buffer[i]);
    // }
    free(entry); // Free the memory after use
    free(buffer); // Free the memory after use
}

// Function to find a file in the current directory
struct FAT32DirEntry* findFileInDirectory(const char* filename) {
    unsigned int sector = cluster_to_sector(&boot_sector, current_directory_cluster);
    struct FAT32DirEntry* entries = malloc(SECTOR_SIZE * boot_sector.sectorsPerCluster / sizeof(struct FAT32DirEntry));

    if (entries == NULL) {
        // Handle memory allocation failure
        printf("Error: Failed to allocate memory for directory entries.\n");
        return NULL;
    }
    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        if (!ata_read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))])) {
            // Handle read error
            free(entries);
            return NULL;
        }
    }

    for (unsigned int j = 0; j < boot_sector.sectorsPerCluster * (SECTOR_SIZE / sizeof(struct FAT32DirEntry)); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            break;
        }

        if (entries[j].name[0] == 0xE5 || (entries[j].attr & 0x0F) == 0x0F) { // Deleted or LFN entry
            continue;
        }

        char currentName[13]; // Format the filename
        formatFilename(currentName, entries[j].name);

        if (strcmp(currentName, filename) == 0) {
            struct FAT32DirEntry* foundEntry = malloc(sizeof(struct FAT32DirEntry));
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

    free(entries); // Free the allocated memory
    return NULL; // File not found
}

bool create_file(const char* filename) {
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


bool delete_file(const char* filename) {
    // 1. Find the directory entry for the file to delete
    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return false;
    }
    // 2. Free the file's cluster chain in the FAT
    if (!free_cluster_chain(&boot_sector, readStartCluster(entry))) {
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
File* open_file(const char* filename, const char* mode) {

    struct FAT32DirEntry* entry = findFileInDirectory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return NULL;
    }

    unsigned int startCluster = readStartCluster(entry);
    int fileSize = entry->fileSize;
    // char* buffer = malloc(fileSize);
    // if (buffer == NULL) {
    //     printf("Not enough memory.\n");
    //     return NULL;
    // }

    File* file = malloc(sizeof(File));
    if (file == NULL) {
        printf("Not enough memory.\n");
        return NULL;
    }

    file->position = 0;
    file->size = fileSize;
    file->ptr = malloc(fileSize);
    file->mode = mode;
    file->name = filename;
    file->startCluster = startCluster;

    //readFileData(startCluster, buffer, fileSize);

    return file;
}

// read file
int read_file(File* file, void* buffer, unsigned int size) {
    if (strcmp(file->mode, "w") == 0) {
        printf("Error: File is not open for reading.\n");
        return 0;
    }

    if (file->position + size > file->size) {
        size = file->size - file->position;
    }

    readFileData(file->startCluster, buffer, size);

    return size;
}
