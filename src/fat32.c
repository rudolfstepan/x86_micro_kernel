#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "fat32.h"
#include "ata.h"
#include "strings.h"

#include "stdlib.h"
#include "stdio.h"


struct Fat32BootSector bootSector;
unsigned int current_directory_cluster = 2; // Default root directory cluster for FAT32

// Helper function to convert a character to uppercase
char to_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? (ch - 'a' + 'A') : ch;
}

int isalnum(int c) {
    // Check if the character is a letter (uppercase or lowercase) or a digit
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

void formatFilename(char* dest, unsigned char* src) {
    int i, j;

    // Copy filename (up to 8 characters, stop at space)
    for (i = 0, j = 0; i < 8 && src[i] != ' '; i++, j++) {
        dest[j] = src[i];
    }

    // If there's an extension, add a dot and copy the extension
    if (src[8] != ' ') {
        dest[j++] = '.';
        for (i = 8; i < 11 && src[i] != ' '; i++, j++) {
            dest[j] = src[i];
        }
    }
    dest[j] = '\0'; // Null-terminate the string
}

// Compares a FAT32 8.3 formatted name with a regular string
int compare_names(const char* fatName, const char* regularName) {
    int i, j;

    // Compare the name part (first 8 characters)
    for (i = 0, j = 0; i < 8; i++) {
        if (regularName[j] == '\0' || regularName[j] == '.') {
            // If we reached the end of the regular name or a dot, the rest of the fatName should be spaces
            if (fatName[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (to_upper(fatName[i]) != to_upper(regularName[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If the regular name has a dot for the extension, skip it
    if (regularName[j] == '.') {
        j++;
    }

    // Compare the extension part (last 3 characters)
    for (i = 8; i < 11; i++) {
        if (regularName[j] == '\0') {
            // If we reached the end of the regular name, the rest of the fatName should be spaces
            if (fatName[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (to_upper(fatName[i]) != to_upper(regularName[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If we reached the end of both strings without a mismatch, it's a match
    return (regularName[j] == '\0') ? 0 : -1;
}

unsigned int get_entries_per_cluster(struct Fat32BootSector* bs) {
    unsigned int clusterSize = bs->bytesPerSector * bs->sectorsPerCluster;
    unsigned int entriesPerCluster = clusterSize / DIRECTORY_ENTRY_SIZE;

    return entriesPerCluster;
}

unsigned int get_total_clusters(struct Fat32BootSector* bs) {
    // Assuming you have a global or accessible boot_sector structure
    // and the structure has fields: totalSectors32, reservedSectorCount,
    // numberOfFATs, FATsizeFAT32, and sectorsPerCluster
    unsigned int totalDataSectors = bs->totalSectors32
        - bs->reservedSectorCount
        - (bs->numberOfFATs * bs->FATSize32);

    unsigned int totalClusters = totalDataSectors / bs->sectorsPerCluster;

    return totalClusters;
}

unsigned int find_free_cluster(struct Fat32BootSector* bs) {
    // Calculate the total number of clusters in the filesystem
    unsigned int totalClusters = get_total_clusters(bs);

    for (unsigned int cluster = 2; cluster < totalClusters; cluster++) {
        if (read_fat_entry(bs, cluster) == 0) {
            // Found a free cluster
            return cluster;
        }
    }

    return INVALID_CLUSTER; // Indicate failure (no free cluster found)
}

bool mark_cluster_in_fat(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value) {
    if (cluster < 2 || cluster >= get_total_clusters(bs)) {
        return false; // Cluster number out of bounds
    }
    // Calculate the FAT entry's position
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = bs->reservedSectorCount + (fatOffset / bs->bytesPerSector);
    unsigned int entOffset = fatOffset % bs->bytesPerSector;
    // Read the sector containing this FAT entry
    unsigned char buffer[bs->bytesPerSector];
    if (!read_sector(fatSector, buffer)) {
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false; // Error reading sector
    }
    // Modify the FAT entry in the buffer
    unsigned int* fatEntry = (unsigned int*)&buffer[entOffset];
    *fatEntry = (*fatEntry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    // Write the modified sector back to the FAT
    if (!write_sector(fatSector, buffer)) {
        printf("Error: Failed to write the modified sector back to the FAT.\n");
        return false; // Error writing sector
    }
    return true;
}

unsigned int get_first_data_sector(struct Fat32BootSector* bs) {
    unsigned int rootDirSectors = ((bs->rootEntryCount * 32) + (bs->bytesPerSector - 1)) / bs->bytesPerSector;
    unsigned int firstDataSector = bs->reservedSectorCount + (bs->numberOfFATs * bs->FATSize32) + rootDirSectors;

    return firstDataSector;
}

bool write_cluster(struct Fat32BootSector* bs, unsigned int cluster, const struct FAT32DirEntry* entries) {
    if (entries == NULL) {
        printf("Error: Entries buffer is null.\n");
        return false; // Error: Buffer is null
    }
    // Calculate the starting sector for this cluster
    unsigned int firstSectorOfCluster = ((cluster - 2) * bs->sectorsPerCluster) + get_first_data_sector(bs);

    for (unsigned int i = 0; i < bs->sectorsPerCluster; i++) {
        // Calculate sector number to write to
        unsigned int sectorNumber = firstSectorOfCluster + i;
        // Calculate the pointer to the part of the entries buffer to write
        const void* bufferPtr = ((const unsigned char*)entries) + (i * bs->bytesPerSector);
        // Write the sector
        if (!write_sector(sectorNumber, bufferPtr)) {
            printf("Error: Failed to write to sector %u.\n", sectorNumber);
            return false; // Error writing sector
        }
    }
    return true;
}

int init_fs() {
    // Read the first sector (LBA 0) into boot_sector
    if (!read_sector(0, &bootSector)) {
        // Handle error (e.g., log error or halt)
        printf("Error reading boot sector.\n");

        return SUCCESS;
    }
    // Update current directory cluster based on the boot sector
    current_directory_cluster = bootSector.rootCluster;
    return FAILURE;
}

unsigned int cluster_to_sector(struct Fat32BootSector* bs, unsigned int cluster) {
    unsigned int firstDataSector = bs->reservedSectorCount + (bs->numberOfFATs * bs->FATSize32);
    return ((cluster - 2) * bs->sectorsPerCluster) + firstDataSector;
}

unsigned int clusterStartSector(struct Fat32BootSector* bs, unsigned int clusterNumber) {
    unsigned int firstDataSector = bs->reservedSectorCount + (bs->numberOfFATs * bs->FATSize32);
    return ((clusterNumber - 2) * bs->sectorsPerCluster) + firstDataSector;
}

void read_cluster(struct Fat32BootSector* bs, unsigned int clusterNumber, void* buffer) {
    unsigned int startSector = clusterStartSector(bs, clusterNumber);
    for (unsigned int i = 0; i < bs->sectorsPerCluster; ++i) {
        read_sector(startSector + i, buffer + (i * SECTOR_SIZE));
    }
}

unsigned int readStartCluster(struct FAT32DirEntry* entry) {
    return ((unsigned int)entry->firstClusterHigh << 16) | entry->firstClusterLow;
}

unsigned int get_next_cluster_in_chain(struct Fat32BootSector* bs, unsigned int currentCluster) {
    unsigned int fatOffset = currentCluster * 4; // 4 bytes per FAT32 entry
    unsigned int fatSector = bs->reservedSectorCount + (fatOffset / bs->bytesPerSector);
    unsigned int entOffset = fatOffset % bs->bytesPerSector;
    // Buffer to read a part of the FAT
    unsigned char buffer[bs->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!read_sector(fatSector, buffer)) {
        // Handle read error
        return INVALID_CLUSTER;
    }
    // Read the 4 bytes of the current cluster's entry from the buffer
    unsigned int nextCluster = *(unsigned int*)&buffer[entOffset];
    // Mask out the high 4 bits (reserved for FAT32)
    nextCluster &= FAT32_EOC_MAX;
    // Check for end of chain markers
    if (nextCluster >= FAT32_EOC_MIN) {
        return INVALID_CLUSTER; // End of chain
    }
    return nextCluster;
}

int isEndOfClusterChain(unsigned int cluster) {
    return cluster >= FAT32_EOC_MIN && cluster <= FAT32_EOC_MAX;
}

// Function to find the next cluster given a directory name and a starting cluster
unsigned int find_next_cluster(struct Fat32BootSector* bs, const char* dirName, unsigned int currentCluster) {
    struct FAT32DirEntry entries[SECTOR_SIZE / sizeof(struct FAT32DirEntry)];
    unsigned int nextCluster = INVALID_CLUSTER;

    do {
        unsigned int sector = cluster_to_sector(bs, currentCluster);
        for (unsigned int i = 0; i < bs->sectorsPerCluster; i++) {
            // Read the entire sector
            if (!read_sector(sector + i, entries)) {
                // Handle read error
                return INVALID_CLUSTER;
            }

            // Parse entries in this sector
            for (unsigned int j = 0; j < SECTOR_SIZE / sizeof(struct FAT32DirEntry); j++) {
                struct FAT32DirEntry entry = entries[j];

                // Check for end of directory
                if (entry.name[0] == 0x00) {
                    return INVALID_CLUSTER; // No more entries, directory not found
                }
                if (entry.name[0] == 0xE5 || (entry.attr & 0x0F) == 0x0F) {
                    continue; // Skip deleted or LFN entries
                }

                // Check if this is the directory we're looking for
                if ((entry.attr & 0x10) && compare_names((const char*)entry.name, dirName) == 0) {
                    nextCluster = ((unsigned int)entry.firstClusterHigh << 16) | entry.firstClusterLow;
                    return nextCluster;
                }
            }
        }

        // Move to the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(bs, currentCluster);
    } while (currentCluster < FAT32_EOC_MIN);

    return INVALID_CLUSTER; // Directory not found
}

void read_cluster_dir_entries(unsigned int currentCluster) {
    unsigned int sector = cluster_to_sector(&bootSector, currentCluster);
    struct FAT32DirEntry entries[SECTOR_SIZE * bootSector.sectorsPerCluster / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
        read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))]);
    }
    for (unsigned int j = 0; j < sizeof(entries) / sizeof(struct FAT32DirEntry); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            break;
        }
        // Überspringen von LFN-Einträgen und gelöschten Einträgen
        if ((entries[j].name[0] == 0xE5) || (entries[j].attr & 0x0F) == 0x0F) {
            continue;
        }
        // Verarbeitung von 8.3-Einträgen
        char currentName[13]; // 8 characters for name, 3 for extension, 1 for null-terminator, 1 for potential dot
        formatFilename(currentName, entries[j].name);
        // Zusätzliche Überprüfung, um sicherzustellen, dass es sich nicht um einen LFN-Eintrag handelt
        if (entries[j].attr & 0x10) {
            printf("[DIR] %s\n", currentName);
        } else {
            printf("%s\n", currentName);
        }
    }
}

void read_cluster_dir_entries_to_buffer(unsigned int currentCluster, char *buffer, unsigned int *size) {
    unsigned int sector = cluster_to_sector(&bootSector, currentCluster);
    struct FAT32DirEntry entries[SECTOR_SIZE * bootSector.sectorsPerCluster / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
        read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))]);
    }
    for (unsigned int j = 0; j < sizeof(entries) / sizeof(struct FAT32DirEntry); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            break;
        }
        // Überspringen von LFN-Einträgen und gelöschten Einträgen
        if ((entries[j].name[0] == 0xE5) || (entries[j].attr & 0x0F) == 0x0F) {
            continue;
        }
        // Verarbeitung von 8.3-Einträgen
        char currentName[13]; // 8 characters for name, 3 for extension, 1 for null-terminator, 1 for potential dot
        formatFilename(currentName, entries[j].name);
        // Zusätzliche Überprüfung, um sicherzustellen, dass es sich nicht um einen LFN-Eintrag handelt
        if (entries[j].attr & 0x10) {
            // Add directory to buffer
            //printf("[DIR] %s\n", currentName);

            strncat(buffer, "[DIR] ", *size - strlen(buffer) - 1);
            strncat(buffer, currentName, *size - strlen(buffer) - 1);
            strncat(buffer, "\n", *size - strlen(buffer) - 1);
        } else {
            // Add file to buffer
            //printf("%s\n", currentName);

            strncat(buffer, currentName, *size - strlen(buffer) - 1);
            strncat(buffer, "\n", *size - strlen(buffer) - 1);
        }
    }

    *size = strlen(buffer); // Update the size with the current length of the buffer

    // printf("Buffer: %s\n", buffer);
    // printf("Size: %d\n", *size);
}

void read_directory() {
    read_cluster_dir_entries(bootSector.rootCluster);
}

// Function to read a directory path and return if it exists
bool read_directory_path(const char* path) {
    unsigned int currentCluster = bootSector.rootCluster; // Assuming boot_sector is defined and initialized elsewhere
    char tempPath[MAX_PATH_LENGTH]; // Temporary path buffer
    strcpy(tempPath, path);
    tempPath[sizeof(tempPath) - 1] = '\0'; // Ensure null-termination
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = tempPath;
    if (start[0] == '/') {
        start++;
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        //printf("Searching for directory: %s\n", token);
        // Find the next directory in the path
        currentCluster = find_next_cluster(&bootSector, token, currentCluster);
        if (currentCluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Now currentCluster points to the cluster of the target directory
    read_cluster_dir_entries(currentCluster);
    return true;
}

int read_directory_to_buffer(const char *path, char *buffer, unsigned int *size){
    unsigned int currentCluster = bootSector.rootCluster; // Assuming boot_sector is defined and initialized elsewhere
    char tempPath[MAX_PATH_LENGTH]; // Temporary path buffer
    strcpy(tempPath, path);
    tempPath[sizeof(tempPath) - 1] = '\0'; // Ensure null-termination
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = tempPath;
    if (start[0] == '/') {
        start++;
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        //printf("Searching for directory: %s\n", token);
        // Find the next directory in the path
        currentCluster = find_next_cluster(&bootSector, token, currentCluster);
        if (currentCluster == INVALID_CLUSTER) {
            //printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Now currentCluster points to the cluster of the target directory
    read_cluster_dir_entries_to_buffer(currentCluster, buffer, size);

    return true;
}

bool try_directory_path(const char* path) {
    unsigned int targetCluster = current_directory_cluster; // Start from the current directory
    char tempPath[MAX_PATH_LENGTH]; // Temporary path buffer
    strcpy(tempPath, path);
    tempPath[sizeof(tempPath) - 1] = '\0'; // Ensure null-termination
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = tempPath;
    if (start[0] == '/') {
        start++;
        targetCluster = bootSector.rootCluster; // Absolute path, start from root
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        // Find the next directory in the path
        targetCluster = find_next_cluster(&bootSector, token, targetCluster);
        if (targetCluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Update current_directory_cluster on successful path change
    current_directory_cluster = targetCluster;
    return true;
}

// Function to read a file's data into a buffer
void readFileData(unsigned int startCluster, char* buffer, unsigned int size) {
    unsigned int currentCluster = startCluster;
    unsigned int bytesRead = 0;
    while (bytesRead < size) {
        unsigned int sectorNumber = cluster_to_sector(&bootSector, currentCluster);
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
            read_sector(sectorNumber + i, buffer + bytesRead);
            bytesRead += SECTOR_SIZE;
            if (bytesRead >= size) {
                break;  // Stop if we have read the required size
            }
        }
        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&bootSector, currentCluster);
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
        unsigned int sectorNumber = cluster_to_sector(&bootSector, currentCluster);
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
            read_sector(sectorNumber + i, bufferPtr);
            bufferPtr += SECTOR_SIZE;
            bytesRead += SECTOR_SIZE;

            if (bytesRead >= fileSize) {
                break; // Stop if we have read the entire file
            }
        }
        // Get the next cluster in the chain
        currentCluster = get_next_cluster_in_chain(&bootSector, currentCluster);
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
    unsigned int sector = cluster_to_sector(&bootSector, current_directory_cluster);
    struct FAT32DirEntry* entries = malloc(SECTOR_SIZE * bootSector.sectorsPerCluster / sizeof(struct FAT32DirEntry));

    if (entries == NULL) {
        // Handle memory allocation failure
        printf("Error: Failed to allocate memory for directory entries.\n");
        return NULL;
    }
    for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
        if (!read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))])) {
            // Handle read error
            free(entries);
            return NULL;
        }
    }

    for (unsigned int j = 0; j < bootSector.sectorsPerCluster * (SECTOR_SIZE / sizeof(struct FAT32DirEntry)); j++) {
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

bool change_directory(const char* path) {
    if (try_directory_path(path)) {
        return true;
    }

    return false;
}

bool create_directory(const char* dirname) {

    printf("Creating directory: %s\n", dirname);
    // 1. Find a free cluster
    unsigned int newDirCluster = find_free_cluster(&bootSector);
    if (newDirCluster == INVALID_CLUSTER) {
        printf("Error: Failed to allocate a new cluster for the directory.\n");
        return false;
    }
    // 2. Update the FAT for the new cluster
    if (!mark_cluster_in_fat(&bootSector, newDirCluster, FAT32_EOC_MAX)) {
        printf("Error: Failed to update the FAT.\n");
        return false;
    }
    // 3. Initialize the new directory's cluster
    struct FAT32DirEntry dirEntries[get_entries_per_cluster(&bootSector)];
    memset(dirEntries, 0, sizeof(dirEntries));
    initialize_new_directory_entries(dirEntries, newDirCluster, current_directory_cluster);

    if (!write_cluster(&bootSector, newDirCluster, dirEntries)) {
        printf("Error: Failed to write the initialized entries to the new cluster.\n");
        return false;
    }
    // 4. Update the parent directory
    if (!add_entry_to_directory(&bootSector, current_directory_cluster, dirname, newDirCluster, ATTR_DIRECTORY)) {
        printf("Error: Failed to update the parent directory.\n");
        return false;
    }
    return true;
}

void initialize_new_directory_entries(struct FAT32DirEntry* entries, unsigned int newDirCluster, unsigned int parentCluster) {
    // Create '.' entry
    create_directory_entry(&entries[0], ".", newDirCluster, ATTR_DIRECTORY);
    // Create '..' entry
    create_directory_entry(&entries[1], "..", parentCluster, ATTR_DIRECTORY);
    // The rest of the entries are already initialized to zero
}

void create_directory_entry(struct FAT32DirEntry* entry, const char* name, unsigned int cluster, unsigned char attributes) {
    // Clear the entry
    memset(entry, 0, sizeof(struct FAT32DirEntry));
    // Convert and set the name in 8.3 format
    convert_to_83_format(entry->name, name);
    // Set the first cluster number
    entry->firstClusterHigh = (cluster >> 16) & 0xFFFF;
    entry->firstClusterLow = cluster & 0xFFFF;

    // Set attributes (e.g., directory attribute)
    entry->attr = attributes;
    // Set creation, last access, and last write time
    // For simplicity, these could be set to a fixed value or the current time
    // Implement set_fat32_time to set these values
    set_fat32_time(&entry->crtTime, &entry->crtDate);
    set_fat32_time(NULL, &entry->lastAccessDate); // Assuming set_fat32_time handles NULL time
    set_fat32_time(&entry->wrtTime, &entry->wrtDate);
}

void convert_to_83_format(unsigned char* dest, const char* src) {
    // Clear the destination array
    memset(dest, ' ', 11); // 8 chars for name, 3 for extension

    // Special handling for "." and ".." entries
    if (strcmp(src, ".") == 0) {
        dest[0] = '.';
        return;
    } else if (strcmp(src, "..") == 0) {
        dest[0] = '.';
        dest[1] = '.';
        return;
    }

    // Copy the name part
    int i = 0, j = 0;
    while (src[i] != '\0' && src[i] != '.' && j < 8) {
        if (isalnum((unsigned char)src[i])) {
            dest[j++] = to_upper((unsigned char)src[i]);
        }
        i++;
    }

    // Skip to the extension part
    while (src[i] != '\0' && src[i] != '.') {
        i++;
    }

    // Copy the extension part
    if (src[i] == '.') {
        i++;
        j = 8; // Extension starts at the 9th position
        while (src[i] != '\0' && j < 11) {
            if (isalnum((unsigned char)src[i])) {
                dest[j++] = to_upper((unsigned char)src[i]);
            }
            i++;
        }
    }
}

void set_fat32_time(unsigned short* time, unsigned short* date) {
    // Fixed date and time for testing
    // Let's set the date to 2024-01-11 and time to 12:00:00
    // Year = 2024 - 1980, Month = 1, Day = 11
    // Hour = 12, Minute = 0, Second = 0
    if (time) {
        *time = (12 << 11) | (0 << 5) | (0 / 2);
    }
    if (date) {
        *date = ((2024 - 1980) << 9) | (1 << 5) | 11;
    }
}

unsigned int allocate_new_cluster(struct Fat32BootSector* bs) {
    // Assuming you have functions to read and write to the FAT
    // and a function to get the total number of clusters in the filesystem
    unsigned int totalClusters = get_total_clusters(bs); // Implement this function
    // Scan the FAT for a free cluster
    for (unsigned int cluster = 2; cluster < totalClusters; cluster++) {
        if (read_fat_entry(bs, cluster) == 0) { // Assuming 0 indicates a free cluster
            // Mark the cluster as used (end of chain)
            if (write_fat_entry(bs, cluster, FAT32_EOC_MAX)) { // Implement this function
                return cluster; // Successfully allocated cluster
            } else {
                return INVALID_CLUSTER; // Failed to update FAT entry
            }
        }
    }

    return INVALID_CLUSTER; // No free clusters available
}

unsigned int read_fat_entry(struct Fat32BootSector* bs, unsigned int cluster) {
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = bs->reservedSectorCount + (fatOffset / bs->bytesPerSector);
    unsigned int entOffset = fatOffset % bs->bytesPerSector;
    // Buffer to read a part of the FAT
    unsigned char buffer[bs->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!read_sector(fatSector, buffer)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return INVALID_CLUSTER;
    }
    // Read the 4 bytes of the current cluster's entry from the buffer
    unsigned int nextCluster = *(unsigned int*)&buffer[entOffset];
    // Mask out the high 4 bits (reserved for FAT32)
    nextCluster &= 0x0FFFFFFF;
    return nextCluster;
}

bool write_fat_entry(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value) {
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = bs->reservedSectorCount + (fatOffset / bs->bytesPerSector);
    unsigned int entOffset = fatOffset % bs->bytesPerSector;
    // Buffer to read and modify a part of the FAT
    unsigned char buffer[bs->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!read_sector(fatSector, buffer)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false;
    }
    // Modify the FAT entry in the buffer
    unsigned int* fatEntry = (unsigned int*)&buffer[entOffset];
    *fatEntry = (*fatEntry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    // Write the modified sector back to the FAT
    if (!write_sector(fatSector, buffer)) {
        // Handle write error
        printf("Error: Failed to write the modified sector back to the FAT.\n");
        return false;
    }
    return true;
}

bool link_cluster_to_chain(struct Fat32BootSector* bs, unsigned int parentCluster, unsigned int newCluster) {
    unsigned int currentCluster = parentCluster;
    unsigned int nextCluster = get_next_cluster_in_chain(bs, currentCluster);

    while (nextCluster < FAT32_EOC_MIN) {
        currentCluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(bs, currentCluster);
    }
    // currentCluster now points to the last cluster in the chain
    // Update the FAT to link the new cluster to the chain
    if (!mark_cluster_in_fat(bs, currentCluster, newCluster)) {
        printf("Error: Failed to link cluster %u to chain starting at %u\n", newCluster, parentCluster);
        return false; // Failed to update the FAT
    }
    return true;
}

bool add_entry_to_directory(struct Fat32BootSector* bs, unsigned int parentCluster, const char* dirname, unsigned int newDirCluster, unsigned char attributes) {
    struct FAT32DirEntry newEntry;
    memset(&newEntry, 0, sizeof(newEntry));  // Initialize new entry to zero
    // Create the new directory entry for 'dirname'
    create_directory_entry(&newEntry, dirname, newDirCluster, attributes);

    unsigned int entriesPerCluster = get_entries_per_cluster(bs);
    struct FAT32DirEntry entries[entriesPerCluster];
    bool entryAdded = false;
    unsigned int currentCluster = parentCluster;

    while (currentCluster < FAT32_EOC_MIN) {
        read_cluster(bs, currentCluster, entries);

        for (unsigned int i = 0; i < entriesPerCluster; ++i) {
            if (entries[i].name[0] == 0 || entries[i].name[0] == 0xE5) {
                entries[i] = newEntry;
                write_cluster(bs, currentCluster, entries);
                entryAdded = true;
                break;
            }
        }

        if (entryAdded) {
            break;
        }

        currentCluster = get_next_cluster_in_chain(bs, currentCluster);
        if (isEndOfClusterChain(currentCluster)) {
            currentCluster = allocate_new_cluster(bs);
            if (currentCluster == INVALID_CLUSTER) {
                printf("Debug: Failed to allocate new cluster\n");
                return false;
            }

            // Link the new cluster to the end of the chain
            if (!mark_cluster_in_fat(bs, parentCluster, currentCluster)) {
                printf("Debug: Failed to link new cluster in FAT\n");
                return false;
            }

            memset(entries, 0, sizeof(entries)); // Initialize the new cluster's entries
            entries[0] = newEntry;
            write_cluster(bs, currentCluster, entries); // Write the new cluster
            entryAdded = true;
        }
    }

    if (entryAdded) {
    } else {
        printf("Failed to add entry. No space available.\n");
    }

    return entryAdded;
}

bool create_file(const char* filename) {
    // 1. Find a free cluster for the new file
    unsigned int newFileCluster = find_free_cluster(&bootSector);
    if (newFileCluster == INVALID_CLUSTER) {
        printf("Failed to allocate a new cluster for the file.\n");
        return false;
    }
    // 2. Update the FAT for the new file cluster
    if (!mark_cluster_in_fat(&bootSector, newFileCluster, FAT32_EOC_MAX)) {
        printf("Failed to update the FAT for the new file cluster.\n");
        return false;
    }
    // 3. Add a directory entry for the new file in the current directory
    if (!add_entry_to_directory(&bootSector, current_directory_cluster, filename, newFileCluster, 0)) { // 0 for normal file attributes
        printf("Failed to add a directory entry for the new file.\n");
        return false;
    }
    return true;
}

bool is_directory_empty(struct FAT32DirEntry* entry) {
    unsigned int cluster = readStartCluster(entry);
    unsigned int sector = cluster_to_sector(&bootSector, cluster);
    struct FAT32DirEntry entries[SECTOR_SIZE / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < bootSector.sectorsPerCluster; i++) {
        read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))]);
    }

    for (unsigned int j = 0; j < sizeof(entries) / sizeof(struct FAT32DirEntry); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            break;
        }
        if (entries[j].name[0] == 0xE5
            || (entries[j].attr & 0x0F) == 0x0F
            || entries[j].name[0] == '.'
            || entries[j].name[1] == '.') { // Deleted or LFN entry
            continue;
        }

        return false; // Directory is not empty
    }
    return true; // Directory is empty
}

bool free_cluster_chain(struct Fat32BootSector* bs, unsigned int startCluster) {
    unsigned int currentCluster = startCluster;
    unsigned int nextCluster = get_next_cluster_in_chain(bs, currentCluster);

    while (nextCluster < FAT32_EOC_MIN) {
        if (!mark_cluster_in_fat(bs, currentCluster, 0)) {
            printf("Error: Failed to free cluster %u\n", currentCluster);
            return false;
        }
        currentCluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(bs, currentCluster);
    }
    return true;
}

bool remove_entry_from_directory(struct Fat32BootSector* bs, unsigned int parentCluster, struct FAT32DirEntry* entry) {
    unsigned int entriesPerCluster = get_entries_per_cluster(bs);
    struct FAT32DirEntry entries[entriesPerCluster];
    unsigned int currentCluster = parentCluster;

    while (currentCluster < FAT32_EOC_MIN) {
        read_cluster(bs, currentCluster, entries);

        for (unsigned int i = 0; i < entriesPerCluster; ++i) {
            if (memcmp(&entries[i], entry, sizeof(struct FAT32DirEntry)) == 0) {
                memset(&entries[i], 0, sizeof(struct FAT32DirEntry));
                write_cluster(bs, currentCluster, entries);
                return true;
            }
        }

        currentCluster = get_next_cluster_in_chain(bs, currentCluster);
    }

    return false;
}

bool delete_directory(const char* dirname) {
    // 1. Find the directory entry for the directory to delete
    struct FAT32DirEntry* entry = findFileInDirectory(dirname);
    if (entry == NULL) {
        printf("Directory not found.\n");
        return false;
    }
    // 2. Check if the directory is empty
    if (!is_directory_empty(entry)) {
        printf("Directory is not empty.\n");
        return false;
    }
    // 3. Free the directory's cluster chain in the FAT
    if (!free_cluster_chain(&bootSector, readStartCluster(entry))) {
        printf("Failed to free the directory's cluster chain.\n");
        return false;
    }
    // 4. Remove the directory entry from the parent directory
    if (!remove_entry_from_directory(&bootSector, current_directory_cluster, entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
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
    if (!free_cluster_chain(&bootSector, readStartCluster(entry))) {
        printf("Failed to free the file's cluster chain.\n");
        return false;
    }
    // 3. Remove the directory entry from the parent directory
    if (!remove_entry_from_directory(&bootSector, current_directory_cluster, entry)) {
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
