#include "fat32.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"


// Function to read a directory path and return if it exists
bool fat32_read_dir(const char* path) {
    unsigned int currentCluster = boot_sector.rootCluster; // Assuming boot_sector is defined and initialized elsewhere
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
        currentCluster = find_next_cluster(&boot_sector, token, currentCluster);
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

bool fat32_change_directory(const char* path) {
    unsigned int targetCluster = current_directory_cluster; // Start from the current directory
    char tempPath[MAX_PATH_LENGTH]; // Temporary path buffer
    strcpy(tempPath, path);
    tempPath[sizeof(tempPath) - 1] = '\0'; // Ensure null-termination
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = tempPath;
    if (start[0] == '/') {
        start++;
        targetCluster = boot_sector.rootCluster; // Absolute path, start from root
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        // Find the next directory in the path
        targetCluster = find_next_cluster(&boot_sector, token, targetCluster);
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

bool fat32_create_dir(const char* dirname) {

    printf("Creating directory: %s\n", dirname);
    // 1. Find a free cluster
    unsigned int newDirCluster = find_free_cluster(&boot_sector);
    if (newDirCluster == INVALID_CLUSTER) {
        printf("Error: Failed to allocate a new cluster for the directory.\n");
        return false;
    }
    // 2. Update the FAT for the new cluster
    if (!mark_cluster_in_fat(&boot_sector, newDirCluster, FAT32_EOC_MAX)) {
        printf("Error: Failed to update the FAT.\n");
        return false;
    }
    // 3. Initialize the new directory's cluster
    struct FAT32DirEntry dirEntries[get_entries_per_cluster(&boot_sector)];
    memset(dirEntries, 0, sizeof(dirEntries));
    initialize_new_directory_entries(dirEntries, newDirCluster, current_directory_cluster);

    if (!write_cluster(&boot_sector, newDirCluster, dirEntries)) {
        printf("Error: Failed to write the initialized entries to the new cluster.\n");
        return false;
    }
    // 4. Update the parent directory
    if (!add_entry_to_directory(&boot_sector, current_directory_cluster, dirname, newDirCluster, ATTR_DIRECTORY)) {
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
    set_fat32_time(&entry->writeTime, &entry->writeDate);
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
        if (is_end_of_cluster_chain(currentCluster)) {
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

bool is_directory_empty(struct FAT32DirEntry* entry) {
    unsigned int cluster = read_start_cluster(entry);
    unsigned int sector = cluster_to_sector(&boot_sector, cluster);
    struct FAT32DirEntry entries[SECTOR_SIZE / sizeof(struct FAT32DirEntry)];

    for (unsigned int i = 0; i < boot_sector.sectorsPerCluster; i++) {
        ata_read_sector(ata_base_address, sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct FAT32DirEntry))], ata_is_master);
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

bool fat32_delete_dir(const char* dirname) {
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
    if (!free_cluster_chain(&boot_sector, read_start_cluster(entry))) {
        printf("Failed to free the directory's cluster chain.\n");
        return false;
    }
    // 4. Remove the directory entry from the parent directory
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster, entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        return false;
    }
    return true;
}