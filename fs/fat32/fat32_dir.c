#include "fat32.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "drivers/bus/drives.h"


// Function to read a directory path and return if it exists
static bool fat32_read_dir_unlocked(const char* path) {
    if (!path || strlen(path) >= MAX_PATH_LENGTH) return false;
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

static bool fat32_change_directory_unlocked(const char* path) {
    if (!path || strlen(path) >= MAX_PATH_LENGTH) return false;
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

static bool fat32_create_dir_unlocked(const char* dirname) {

    if (!dirname) return false;

    if (!fat32_is_valid_short_name(dirname) ||
        strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return false;
    }
    struct fat32_dir_entry existing;
    if (fat32_lookup_entry_in_directory(current_directory_cluster, dirname,
                                        &existing) !=
            FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }
    // 1. Allocate and publish ownership of a new cluster.
    unsigned int new_dir_cluster = allocate_new_cluster(&boot_sector);
    if (new_dir_cluster == INVALID_CLUSTER) {
        printf("Error: Failed to allocate a new cluster for the directory.\n");
        return false;
    }
    
    // 3. Initialize the new directory's cluster
    unsigned int entries_per_cluster = get_entries_per_cluster(&boot_sector);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* dir_entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!dir_entries) {
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);
        return false;
    }
    memset(dir_entries, 0, entries_size);
    initialize_new_directory_entries(dir_entries, new_dir_cluster, current_directory_cluster);

    if (!write_cluster(&boot_sector, new_dir_cluster, dir_entries)) {
        printf("Error: Failed to write the initialized entries to the new cluster.\n");
        // ROLLBACK: Free the allocated cluster
        printf("Rolling back: Freeing allocated cluster %u\n", new_dir_cluster);
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);  // Mark as free
        free(dir_entries);
        return false;
    }
    free(dir_entries);
    
    // 4. Update the parent directory
    bool safe_to_reclaim_target = true;
    if (!add_entry_to_directory_checked(
            &boot_sector, current_directory_cluster, dirname,
            new_dir_cluster, ATTR_DIRECTORY, &safe_to_reclaim_target)) {
        printf("Error: Failed to update the parent directory.\n");
        struct fat32_dir_entry observed;
        fat32_lookup_result_t observed_result =
            fat32_lookup_entry_in_directory(current_directory_cluster,
                                            dirname, &observed);
        if (observed_result == FAT32_LOOKUP_FOUND &&
            read_start_cluster(&observed) == new_dir_cluster &&
            (observed.attr & ATTR_DIRECTORY)) {
            /* The parent write committed but its immediate verification was
             * inconclusive. Ownership is established by the re-read. */
        } else if (observed_result == FAT32_LOOKUP_NOT_FOUND &&
                   safe_to_reclaim_target) {
            (void)mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);
            return false;
        } else {
            printf("Warning: preserving ambiguous directory cluster %u\n",
                   new_dir_cluster);
            return false;
        }
    }
    
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

bool add_entry_to_directory_checked(struct fat32_boot_sector* bs,
                                    unsigned int parent_cluster,
                                    const char* dirname,
                                    unsigned int new_dir_cluster,
                                    unsigned char attributes,
                                    bool* safe_to_reclaim_target) {
    if (safe_to_reclaim_target) *safe_to_reclaim_target = true;
    if (!bs || !fat32_is_valid_short_name(dirname) ||
        !is_valid_cluster(bs, parent_cluster) ||
        (new_dir_cluster != 0 && !is_valid_cluster(bs, new_dir_cluster))) {
        return false;
    }

    struct fat32_dir_entry duplicate;
    fat32_lookup_result_t duplicate_result =
        fat32_lookup_entry_in_directory(parent_cluster, dirname, &duplicate);
    if (duplicate_result != FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }

    struct fat32_dir_entry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));  // Initialize new entry to zero
    // Create the new directory entry for 'dirname'
    create_directory_entry(&new_entry, dirname, new_dir_cluster, attributes);

    unsigned int entries_per_cluster = get_entries_per_cluster(bs);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!entries) {
        return false;
    }
    unsigned int current_cluster = parent_cluster;
    unsigned int traversed = 0;
    unsigned int cluster_limit = get_total_clusters(bs);

    while (is_valid_cluster(bs, current_cluster) &&
           traversed++ < cluster_limit) {
        if (!read_cluster(bs, current_cluster, entries)) {
            free(entries);
            return false;
        }

        for (unsigned int i = 0; i < entries_per_cluster; ++i) {
            if (entries[i].name[0] == 0 || entries[i].name[0] == 0xE5) {
                struct fat32_dir_entry original = entries[i];
                entries[i] = new_entry;
                bool ok = write_cluster(bs, current_cluster, entries);
                if (!ok) {
                    if (read_cluster(bs, current_cluster, entries)) {
                        if (memcmp(&entries[i], &new_entry,
                                   sizeof(new_entry)) == 0) {
                            ok = true;
                        } else if (memcmp(&entries[i], &original,
                                          sizeof(original)) != 0 &&
                                   safe_to_reclaim_target) {
                            *safe_to_reclaim_target = false;
                        }
                    } else if (safe_to_reclaim_target) {
                        *safe_to_reclaim_target = false;
                    }
                }
                free(entries);
                return ok;
            }
        }

        unsigned int next = get_next_cluster_in_chain(bs, current_cluster);
        if (next == INVALID_CLUSTER) {
            free(entries);
            return false;
        }
        if (is_end_of_cluster_chain(next)) {
            unsigned int new_cluster = allocate_new_cluster(bs);
            if (new_cluster == INVALID_CLUSTER) {
                free(entries);
                return false;
            }

            memset(entries, 0, entries_size);
            entries[0] = new_entry;
            if (!write_cluster(bs, new_cluster, entries)) {
                mark_cluster_in_fat(bs, new_cluster, 0);
                free(entries);
                return false;
            }
            if (!mark_cluster_in_fat(bs, current_cluster, new_cluster)) {
                uint32_t observed =
                    get_next_cluster_in_chain(bs, current_cluster);
                if (observed != new_cluster) {
                    if (is_end_of_cluster_chain(observed)) {
                        (void)mark_cluster_in_fat(bs, new_cluster, 0);
                    } else {
                        if (safe_to_reclaim_target) {
                            *safe_to_reclaim_target = false;
                        }
                        printf("Warning: preserving ambiguous directory extension %u\n",
                               new_cluster);
                    }
                    free(entries);
                    return false;
                }
            }
            free(entries);
            return true;
        }
        current_cluster = next;
    }

    free(entries);
    return false;
}

bool add_entry_to_directory(struct fat32_boot_sector* bs,
                            unsigned int parent_cluster,
                            const char* dirname,
                            unsigned int new_dir_cluster,
                            unsigned char attributes) {
    bool safe_to_reclaim_target = true;
    return add_entry_to_directory_checked(
        bs, parent_cluster, dirname, new_dir_cluster, attributes,
        &safe_to_reclaim_target);
}

bool is_directory_empty(struct fat32_dir_entry* entry) {
    if (!entry || !(entry->attr & ATTR_DIRECTORY)) {
        return false;
    }

    unsigned int cluster = read_start_cluster(entry);
    if (!is_valid_cluster(&boot_sector, cluster)) {
        return false;
    }

    unsigned int entries_per_cluster = get_entries_per_cluster(&boot_sector);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!entries) {
        return false;
    }

    uint32_t traversed = 0;
    uint32_t limit = get_total_clusters(&boot_sector);
    while (is_valid_cluster(&boot_sector, cluster) && traversed++ < limit) {
        if (!read_cluster(&boot_sector, cluster, entries)) {
            free(entries);
            return false;
        }

        for (unsigned int j = 0; j < entries_per_cluster; j++) {
            if (entries[j].name[0] == 0x00) {
                free(entries);
                return true;
            }
            bool dot = entries[j].name[0] == '.' && entries[j].name[1] == ' ';
            bool dotdot = entries[j].name[0] == '.' &&
                          entries[j].name[1] == '.' &&
                          entries[j].name[2] == ' ';
            if (entries[j].name[0] == 0xE5 ||
                (entries[j].attr & 0x0F) == 0x0F || dot || dotdot) {
                continue;
            }
            free(entries);
            return false;
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, cluster);
        if (next == INVALID_CLUSTER) {
            free(entries);
            return false;
        }
        if (is_end_of_cluster_chain(next)) {
            free(entries);
            return true;
        }
        cluster = next;
    }
    free(entries);
    return false;
}

static bool fat32_delete_dir_unlocked(const char* dirname) {
    if (!dirname || !fat32_is_valid_short_name(dirname) ||
        strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return false;
    }
    // 1. Find the directory entry for the directory to delete
    struct fat32_dir_entry* entry = find_file_in_directory(dirname);
    if (entry == NULL) {
        printf("Directory not found.\n");
        return false;
    }
    if (!(entry->attr & ATTR_DIRECTORY) || (entry->attr & ATTR_READ_ONLY)) {
        free(entry);
        return false;
    }
    // 2. Check if the directory is empty
    if (!is_directory_empty(entry)) {
        printf("Directory is not empty.\n");
        free(entry);
        return false;
    }
    unsigned int start_cluster = read_start_cluster(entry);
    // Remove the name before freeing its storage, avoiding a visible dangling
    // entry if a later FAT update fails.
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster,
                                     entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        free(entry);
        return false;
    }
    if (!free_cluster_chain(&boot_sector, start_cluster)) {
        printf("Failed to free the directory's cluster chain.\n");
        free(entry);
        return false;
    }
    free(entry);
    
    // Sync FSInfo after directory deletion
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

bool fat32_read_dir(const char* path) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_read_dir_unlocked(path);
    fat32_operation_end(flags);
    return result;
}

bool fat32_change_directory(const char* path) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_change_directory_unlocked(path);
    fat32_operation_end(flags);
    return result;
}

bool fat32_create_dir(const char* dirname) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_create_dir_unlocked(dirname);
    fat32_operation_end(flags);
    return result;
}

bool fat32_delete_dir(const char* dirname) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_delete_dir_unlocked(dirname);
    fat32_operation_end(flags);
    return result;
}
