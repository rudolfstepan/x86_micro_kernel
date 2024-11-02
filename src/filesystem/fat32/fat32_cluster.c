#include "fat32.h"
#include "toolchain/stdio.h"

// --------------------------------------------------------------------
// get_entries_per_cluster
// Calculates the number of directory entries that can fit in a cluster
// --------------------------------------------------------------------
unsigned int get_entries_per_cluster(struct Fat32BootSector* boot_sector) {
    unsigned int clusterSize = boot_sector->bytesPerSector * boot_sector->sectorsPerCluster;
    unsigned int entriesPerCluster = clusterSize / DIRECTORY_ENTRY_SIZE;

    return entriesPerCluster;
}

// --------------------------------------------------------------------
// get_total_clusters
// Calculates the total number of clusters in the filesystem
// --------------------------------------------------------------------
unsigned int get_total_clusters(struct Fat32BootSector* boot_sector) {
    // Assuming you have a global or accessible boot_sector structure
    // and the structure has fields: totalSectors32, reservedSectorCount,
    // numberOfFATs, FATsizeFAT32, and sectorsPerCluster
    unsigned int totalDataSectors = boot_sector->totalSectors32
        - boot_sector->reservedSectorCount
        - (boot_sector->numberOfFATs * boot_sector->FATSize32);

    unsigned int totalClusters = totalDataSectors / boot_sector->sectorsPerCluster;

    return totalClusters;
}

// --------------------------------------------------------------------
// find_free_cluster
// Finds the first free cluster in the filesystem
// Returns the cluster number if found, or INVALID_CLUSTER if not found
// --------------------------------------------------------------------
unsigned int find_free_cluster(struct Fat32BootSector* boot_sector) {
    // Calculate the total number of clusters in the filesystem
    unsigned int totalClusters = get_total_clusters(boot_sector);

    for (unsigned int cluster = 2; cluster < totalClusters; cluster++) {
        if (read_fat_entry(boot_sector, cluster) == 0) {
            // Found a free cluster
            return cluster;
        }
    }

    return INVALID_CLUSTER; // Indicate failure (no free cluster found)
}

// --------------------------------------------------------------------
// mark_cluster_in_fat
// Marks a cluster in the FAT with the specified value
// Returns true if successful, false otherwise
// --------------------------------------------------------------------
bool mark_cluster_in_fat(struct Fat32BootSector* boot_sector, unsigned int cluster, unsigned int value) {
    if (cluster < 2 || cluster >= get_total_clusters(boot_sector)) {
        return false; // Cluster number out of bounds
    }
    // Calculate the FAT entry's position
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = boot_sector->reservedSectorCount + (fatOffset / boot_sector->bytesPerSector);
    unsigned int entOffset = fatOffset % boot_sector->bytesPerSector;
    // Read the sector containing this FAT entry
    unsigned char buffer[boot_sector->bytesPerSector];
    if (!ata_read_sector(fatSector, buffer)) {
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false; // Error reading sector
    }
    // Modify the FAT entry in the buffer
    unsigned int* fatEntry = (unsigned int*)&buffer[entOffset];
    *fatEntry = (*fatEntry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    // Write the modified sector back to the FAT
    if (!ata_write_sector(fatSector, buffer)) {
        printf("Error: Failed to write the modified sector back to the FAT.\n");
        return false; // Error writing sector
    }
    return true;
}

// --------------------------------------------------------------------
// get_first_data_sector
// Calculates the first sector of the data region in the filesystem
// --------------------------------------------------------------------
unsigned int get_first_data_sector(struct Fat32BootSector* boot_sector) {
    unsigned int rootDirSectors = ((boot_sector->rootEntryCount * 32) + (boot_sector->bytesPerSector - 1)) / boot_sector->bytesPerSector;
    unsigned int firstDataSector = boot_sector->reservedSectorCount + (boot_sector->numberOfFATs * boot_sector->FATSize32) + rootDirSectors;

    return firstDataSector;
}

// --------------------------------------------------------------------
// write_cluster
// Writes the specified entries to the specified cluster
// Returns true if successful, false otherwise
// --------------------------------------------------------------------
bool write_cluster(struct Fat32BootSector* boot_sector, unsigned int cluster, const struct FAT32DirEntry* entries) {
    if (entries == NULL) {
        printf("Error: Entries buffer is null.\n");
        return false; // Error: Buffer is null
    }
    // Calculate the starting sector for this cluster
    unsigned int firstSectorOfCluster = ((cluster - 2) * boot_sector->sectorsPerCluster) + get_first_data_sector(boot_sector);

    for (unsigned int i = 0; i < boot_sector->sectorsPerCluster; i++) {
        // Calculate sector number to write to
        unsigned int sectorNumber = firstSectorOfCluster + i;
        // Calculate the pointer to the part of the entries buffer to write
        const void* bufferPtr = ((const unsigned char*)entries) + (i * boot_sector->bytesPerSector);
        // Write the sector
        if (!ata_write_sector(sectorNumber, bufferPtr)) {
            printf("Error: Failed to write to sector %u.\n", sectorNumber);
            return false; // Error writing sector
        }
    }
    return true;
}
// return the start sector of a cluster
unsigned int cluster_to_sector(struct Fat32BootSector* boot_sector, unsigned int cluster) {
    unsigned int firstDataSector = boot_sector->reservedSectorCount + (boot_sector->numberOfFATs * boot_sector->FATSize32);
    return ((cluster - 2) * boot_sector->sectorsPerCluster) + firstDataSector;
}

void read_cluster(struct Fat32BootSector* boot_sector, unsigned int clusterNumber, void* buffer) {
    unsigned int startSector = cluster_to_sector(boot_sector, clusterNumber);
    for (unsigned int i = 0; i < boot_sector->sectorsPerCluster; ++i) {
        ata_read_sector(startSector + i, buffer + (i * SECTOR_SIZE));
    }
}

unsigned int readStartCluster(struct FAT32DirEntry* entry) {
    return ((unsigned int)entry->firstClusterHigh << 16) | entry->firstClusterLow;
}

unsigned int get_next_cluster_in_chain(struct Fat32BootSector* boot_sector, unsigned int currentCluster) {
    unsigned int fatOffset = currentCluster * 4; // 4 bytes per FAT32 entry
    unsigned int fatSector = boot_sector->reservedSectorCount + (fatOffset / boot_sector->bytesPerSector);
    unsigned int entOffset = fatOffset % boot_sector->bytesPerSector;
    // Buffer to read a part of the FAT
    unsigned char buffer[boot_sector->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(fatSector, buffer)) {
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

bool isEndOfClusterChain(unsigned int cluster) {
    return cluster >= FAT32_EOC_MIN && cluster <= FAT32_EOC_MAX;
}

// Function to find the next cluster given a directory name and a starting cluster
unsigned int find_next_cluster(struct Fat32BootSector* boot_sector, const char* dirName, unsigned int currentCluster) {
    struct FAT32DirEntry entries[SECTOR_SIZE / sizeof(struct FAT32DirEntry)];
    unsigned int nextCluster = INVALID_CLUSTER;

    do {
        unsigned int sector = cluster_to_sector(boot_sector, currentCluster);
        for (unsigned int i = 0; i < boot_sector->sectorsPerCluster; i++) {
            // Read the entire sector
            if (!ata_read_sector(sector + i, entries)) {
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
        currentCluster = get_next_cluster_in_chain(boot_sector, currentCluster);
    } while (currentCluster < FAT32_EOC_MIN);

    return INVALID_CLUSTER; // Directory not found
}

void read_cluster_dir_entries(unsigned int currentCluster) {
    unsigned int sector = cluster_to_sector(&boot_sector, currentCluster);
    struct FAT32DirEntry entries[SECTOR_SIZE * boot_sector.sectorsPerCluster / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        ata_read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))]);
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
    unsigned int sector = cluster_to_sector(&boot_sector, currentCluster);
    struct FAT32DirEntry entries[SECTOR_SIZE * boot_sector.sectorsPerCluster / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        ata_read_sector(sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))]);
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

unsigned int allocate_new_cluster(struct Fat32BootSector* boot_sector) {
    // Assuming you have functions to read and write to the FAT
    // and a function to get the total number of clusters in the filesystem
    unsigned int totalClusters = get_total_clusters(boot_sector); // Implement this function
    // Scan the FAT for a free cluster
    for (unsigned int cluster = 2; cluster < totalClusters; cluster++) {
        if (read_fat_entry(boot_sector, cluster) == 0) { // Assuming 0 indicates a free cluster
            // Mark the cluster as used (end of chain)
            if (write_fat_entry(boot_sector, cluster, FAT32_EOC_MAX)) { // Implement this function
                return cluster; // Successfully allocated cluster
            } else {
                return INVALID_CLUSTER; // Failed to update FAT entry
            }
        }
    }

    return INVALID_CLUSTER; // No free clusters available
}

bool link_cluster_to_chain(struct Fat32BootSector* boot_sector, unsigned int parentCluster, unsigned int newCluster) {
    unsigned int currentCluster = parentCluster;
    unsigned int nextCluster = get_next_cluster_in_chain(boot_sector, currentCluster);

    while (nextCluster < FAT32_EOC_MIN) {
        currentCluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(boot_sector, currentCluster);
    }
    // currentCluster now points to the last cluster in the chain
    // Update the FAT to link the new cluster to the chain
    if (!mark_cluster_in_fat(boot_sector, currentCluster, newCluster)) {
        printf("Error: Failed to link cluster %u to chain starting at %u\n", newCluster, parentCluster);
        return false; // Failed to update the FAT
    }
    return true;
}

bool free_cluster_chain(struct Fat32BootSector* boot_sector, unsigned int startCluster) {
    unsigned int currentCluster = startCluster;
    unsigned int nextCluster = get_next_cluster_in_chain(boot_sector, currentCluster);

    while (nextCluster < FAT32_EOC_MIN) {
        if (!mark_cluster_in_fat(boot_sector, currentCluster, 0)) {
            printf("Error: Failed to free cluster %u\n", currentCluster);
            return false;
        }
        currentCluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(boot_sector, currentCluster);
    }
    return true;
}
