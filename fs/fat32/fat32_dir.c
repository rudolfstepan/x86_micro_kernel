#include "fat32.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"


// Function to read a directory path and return if it exists
bool fat32_read_dir(const char* path) {
    unsigned int current_cluster = boot_sector.root_cluster; // Assuming boot_sector is defined and initialized elsewhere
    char temp_path[MAX_PATH_LENGTH]; // Temporary path buffer
    
    // Safe string copy with bounds checking
    strncpy(temp_path, path, MAX_PATH_LENGTH - 1);
    temp_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = temp_path;
    if (start[0] == '/') {
        start++;
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        //printf("Searching for directory: %s\n", token);
        // Find the next directory in the path
        current_cluster = find_next_cluster(&boot_sector, token, current_cluster);
        if (current_cluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Now current_cluster points to the cluster of the target directory
    read_cluster_dir_entries(current_cluster);
    return true;
}

bool fat32_change_directory(const char* path) {
    unsigned int target_cluster = current_directory_cluster; // Start from the current directory
    char temp_path[MAX_PATH_LENGTH]; // Temporary path buffer
    
    // Safe string copy with bounds checking
    strncpy(temp_path, path, MAX_PATH_LENGTH - 1);
    temp_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = temp_path;
    if (start[0] == '/') {
        start++;
        target_cluster = boot_sector.root_cluster; // Absolute path, start from root
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        // Find the next directory in the path
        target_cluster = find_next_cluster(&boot_sector, token, target_cluster);
        if (target_cluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Update current_directory_cluster on successful path change
    current_directory_cluster = target_cluster;
    return true;
}

bool fat32_create_dir(const char* dirname) {

    printf("Creating directory: %s\n", dirname);
    // 1. Find a free cluster
    unsigned int new_dir_cluster = find_free_cluster(&boot_sector);
    if (new_dir_cluster == INVALID_CLUSTER) {
        printf("Error: Failed to allocate a new cluster for the directory.\n");
        return false;
    }
    
    // 2. Update the FAT for the new cluster
    if (!mark_cluster_in_fat(&boot_sector, new_dir_cluster, FAT32_EOC_MAX)) {
        printf("Error: Failed to update the FAT.\n");
        // Rollback not needed - cluster is still marked as free
        return false;
    }
    
    // 3. Initialize the new directory's cluster
    struct fat32_dir_entry dir_entries[get_entries_per_cluster(&boot_sector)];
    memset(dir_entries, 0, sizeof(dir_entries));
    initialize_new_directory_entries(dir_entries, new_dir_cluster, current_directory_cluster);

    if (!write_cluster(&boot_sector, new_dir_cluster, dir_entries)) {
        printf("Error: Failed to write the initialized entries to the new cluster.\n");
        // ROLLBACK: Free the allocated cluster
        printf("Rolling back: Freeing allocated cluster %u\n", new_dir_cluster);
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);  // Mark as free
        return false;
    }
    
    // 4. Update the parent directory
    if (!add_entry_to_directory(&boot_sector, current_directory_cluster, dirname, new_dir_cluster, ATTR_DIRECTORY)) {
        printf("Error: Failed to update the parent directory.\n");
        // ROLLBACK: Free the allocated cluster (directory data is orphaned but cluster is freed)
        printf("Rolling back: Freeing allocated cluster %u\n", new_dir_cluster);
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);  // Mark as free
        return false;
    }
    
    printf("Directory '%s' created successfully at cluster %u\n", dirname, new_dir_cluster);
    
    // Sync FSInfo after directory creation
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

void initialize_new_directory_entries(struct fat32_dir_entry* entries, unsigned int new_dir_cluster, unsigned int parent_cluster) {
    // Create '.' entry
    create_directory_entry(&entries[0], ".", new_dir_cluster, ATTR_DIRECTORY);
    // Create '..' entry
    create_directory_entry(&entries[1], "..", parent_cluster, ATTR_DIRECTORY);
    // The rest of the entries are already initialized to zero
}

void create_directory_entry(struct fat32_dir_entry* entry, const char* name, unsigned int cluster, unsigned char attributes) {
    // Clear the entry
    memset(entry, 0, sizeof(struct fat32_dir_entry));
    // Convert and set the name in 8.3 format
    convert_to_83_format(entry->name, name);
    // Set the first cluster number
    entry->first_cluster_high = (cluster >> 16) & 0xFFFF;
    entry->first_cluster_low = cluster & 0xFFFF;

    // Set attributes (e.g., directory attribute)
    entry->attr = attributes;
    
    // Set creation, last access, and last write time using real RTC values
    set_fat32_time(&entry->crt_time, &entry->crt_date);
    set_fat32_time(NULL, &entry->last_access_date); // Date only for last access
    set_fat32_time(&entry->write_time, &entry->write_date);
    
    // Set creation time tenths (0-199 for 0.0-1.9 seconds, we'll use 0 for simplicity)
    entry->crt_time_tenth = 0;
}

bool add_entry_to_directory(struct fat32_boot_sector* bs, unsigned int parent_cluster, const char* dirname, unsigned int new_dir_cluster, unsigned char attributes) {
    struct fat32_dir_entry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));  // Initialize new entry to zero
    // Create the new directory entry for 'dirname'
    create_directory_entry(&new_entry, dirname, new_dir_cluster, attributes);

    unsigned int entries_per_cluster = get_entries_per_cluster(bs);
    struct fat32_dir_entry entries[entries_per_cluster];
    bool entry_added = false;
    unsigned int current_cluster = parent_cluster;

    while (current_cluster < FAT32_EOC_MIN) {
        read_cluster(bs, current_cluster, entries);

        for (unsigned int i = 0; i < entries_per_cluster; ++i) {
            if (entries[i].name[0] == 0 || entries[i].name[0] == 0xE5) {
                entries[i] = new_entry;
                write_cluster(bs, current_cluster, entries);
                entry_added = true;
                break;
            }
        }

        if (entry_added) {
            break;
        }

        current_cluster = get_next_cluster_in_chain(bs, current_cluster);
        if (is_end_of_cluster_chain(current_cluster)) {
            current_cluster = allocate_new_cluster(bs);
            if (current_cluster == INVALID_CLUSTER) {
                printf("Debug: Failed to allocate new cluster\n");
                return false;
            }

            // Link the new cluster to the end of the chain
            if (!mark_cluster_in_fat(bs, parent_cluster, current_cluster)) {
                printf("Debug: Failed to link new cluster in FAT\n");
                return false;
            }

            memset(entries, 0, sizeof(entries)); // Initialize the new cluster's entries
            entries[0] = new_entry;
            write_cluster(bs, current_cluster, entries); // Write the new cluster
            entry_added = true;
        }
    }

    if (entry_added) {
    } else {
        printf("Failed to add entry. No space available.\n");
    }

    return entry_added;
}

bool is_directory_empty(struct fat32_dir_entry* entry) {
    unsigned int cluster = read_start_cluster(entry);
    unsigned int sector = cluster_to_sector(&boot_sector, cluster);
    struct fat32_dir_entry entries[SECTOR_SIZE / sizeof(struct fat32_dir_entry)];

    for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        ata_read_sector(ata_base_address, sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct fat32_dir_entry))], ata_is_master);
    }

    for (unsigned int j = 0; j < sizeof(entries) / sizeof(struct fat32_dir_entry); j++) {
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
    struct fat32_dir_entry* entry = find_file_in_directory(dirname);
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
    
    // Sync FSInfo after directory deletion
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}