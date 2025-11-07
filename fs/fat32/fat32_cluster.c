#include "fat32.h"
#include "lib/libc/stdio.h"

// --------------------------------------------------------------------
// is_valid_cluster
// Validates that a cluster number is within valid range
// --------------------------------------------------------------------
bool is_valid_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    if (cluster < 2) {
        return false; // Clusters 0 and 1 are reserved
    }
    if (cluster >= FAT32_EOC_MIN) {
        return true; // End-of-chain marker is valid in context
    }
    if (cluster >= get_total_clusters(boot_sector)) {
        return false; // Beyond filesystem boundary
    }
    return true;
}

// --------------------------------------------------------------------
// get_entries_per_cluster
// Calculates the number of directory entries that can fit in a cluster
// --------------------------------------------------------------------
unsigned int get_entries_per_cluster(struct fat32_boot_sector* boot_sector) {
    unsigned int cluster_size = boot_sector->bytes_per_sector * boot_sector->sectors_per_cluster;
    unsigned int entries_per_cluster = cluster_size / DIRECTORY_ENTRY_SIZE;

    return entries_per_cluster;
}

// --------------------------------------------------------------------
// get_total_clusters
// Calculates the total number of clusters in the filesystem
// --------------------------------------------------------------------
unsigned int get_total_clusters(struct fat32_boot_sector* boot_sector) {
    // Assuming you have a global or accessible boot_sector structure
    // and the structure has fields: totalSectors32, reservedSectorCount,
    // numberOfFATs, FATsizeFAT32, and sectorsPerCluster
    unsigned int total_data_sectors = boot_sector->total_sectors_32
        - boot_sector->reserved_sector_count
        - (boot_sector->number_of_fats * boot_sector->fat_size_32);

    unsigned int total_clusters = total_data_sectors / boot_sector->sectors_per_cluster;

    return total_clusters;
}

// --------------------------------------------------------------------
// find_free_cluster
// Finds the first free cluster in the filesystem
// Returns the cluster number if found, or INVALID_CLUSTER if not found
// Uses FSInfo hint if available for faster allocation
// --------------------------------------------------------------------
unsigned int find_free_cluster(struct fat32_boot_sector* boot_sector) {
    extern struct fat32_fsinfo fsinfo;
    extern bool fsinfo_valid;
    
    unsigned int total_clusters = get_total_clusters(boot_sector);
    unsigned int start_cluster = 2;
    
    // Use FSInfo hint if available
    if (fsinfo_valid && fsinfo.next_free_cluster != 0xFFFFFFFF && 
        fsinfo.next_free_cluster >= 2 && fsinfo.next_free_cluster < total_clusters) {
        start_cluster = fsinfo.next_free_cluster;
        printf("Using FSInfo hint: starting search at cluster %u\n", start_cluster);
    }
    
    // Search from hint to end
    for (unsigned int cluster = start_cluster; cluster < total_clusters; cluster++) {
        if (read_fat_entry(boot_sector, cluster) == 0) {
            // Found a free cluster - update FSInfo hint
            if (fsinfo_valid) {
                fsinfo.next_free_cluster = cluster + 1;
            }
            return cluster;
        }
    }
    
    // Wrap around: search from beginning to hint
    if (start_cluster > 2) {
        for (unsigned int cluster = 2; cluster < start_cluster; cluster++) {
            if (read_fat_entry(boot_sector, cluster) == 0) {
                // Found a free cluster - update FSInfo hint
                if (fsinfo_valid) {
                    fsinfo.next_free_cluster = cluster + 1;
                }
                return cluster;
            }
        }
    }

    return INVALID_CLUSTER; // No free cluster found
}

// --------------------------------------------------------------------
// mark_cluster_in_fat
// Marks a cluster in the FAT with the specified value
// Returns true if successful, false otherwise
// --------------------------------------------------------------------
bool mark_cluster_in_fat(struct fat32_boot_sector* boot_sector, unsigned int cluster, unsigned int value) {
    if (cluster < 2 || cluster >= get_total_clusters(boot_sector)) {
        return false; // Cluster number out of bounds
    }
    // Calculate the FAT entry's position
    unsigned int fat_offset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fat_sector = partition_lba_offset + boot_sector->reserved_sector_count + (fat_offset / boot_sector->bytes_per_sector);
    unsigned int ent_offset = fat_offset % boot_sector->bytes_per_sector;
    // Read the sector containing this FAT entry
    unsigned char buffer[boot_sector->bytes_per_sector];
    if (!ata_read_sector(ata_base_address, fat_sector, buffer, ata_is_master)) {
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false; // Error reading sector
    }
    
    // Modify the FAT entry in the buffer
    unsigned int* fat_entry_ptr = (unsigned int*)&buffer[ent_offset];
    unsigned int old_value = *fat_entry_ptr & 0x0FFFFFFF;
    *fat_entry_ptr = (*fat_entry_ptr & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    
    // Write the modified sector back to ALL FAT copies (mirroring for redundancy)
    for (unsigned int fat_num = 0; fat_num < boot_sector->number_of_fats; fat_num++) {
        unsigned int fat_sector_offset = fat_num * boot_sector->fat_size_32;
        unsigned int current_fat_sector = partition_lba_offset + boot_sector->reserved_sector_count + fat_sector_offset + (fat_offset / boot_sector->bytes_per_sector);
        
        if (!ata_write_sector(ata_base_address, current_fat_sector, buffer, ata_is_master)) {
            printf("Error: Failed to write to FAT copy %u at sector %u\n", fat_num, current_fat_sector);
            return false; // Error writing sector
        }
        
        // Verify the write by reading back the sector
        unsigned char verify_buffer[boot_sector->bytes_per_sector];
        if (!ata_read_sector(ata_base_address, current_fat_sector, verify_buffer, ata_is_master)) {
            printf("Error: Failed to read back FAT copy %u for verification\n", fat_num);
            return false;
        }
        
        // Compare the written data
        if (memcmp(buffer, verify_buffer, boot_sector->bytes_per_sector) != 0) {
            printf("Error: Write verification failed for FAT copy %u\n", fat_num);
            return false;
        }
    }
    
    // Update FSInfo if cluster allocation changed
    extern void update_fsinfo_free_count(int delta);
    if (old_value == 0 && value != 0) {
        // Cluster was allocated
        update_fsinfo_free_count(-1);
    } else if (old_value != 0 && value == 0) {
        // Cluster was freed
        update_fsinfo_free_count(1);
    }
    
    return true;
}

// --------------------------------------------------------------------
// get_first_data_sector
// Calculates the first sector of the data region in the filesystem
// --------------------------------------------------------------------
unsigned int get_first_data_sector(struct fat32_boot_sector* boot_sector) {
    unsigned int root_dir_sectors = ((boot_sector->root_entry_count * 32) + (boot_sector->bytes_per_sector - 1)) / boot_sector->bytes_per_sector;
    unsigned int first_data_sector = partition_lba_offset + boot_sector->reserved_sector_count + (boot_sector->number_of_fats * boot_sector->fat_size_32) + root_dir_sectors;

    return first_data_sector;
}

// --------------------------------------------------------------------
// write_cluster
// Writes the specified entries to the specified cluster
// Returns true if successful, false otherwise
// --------------------------------------------------------------------
bool write_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster, const struct fat32_dir_entry* entries) {
    if (entries == NULL) {
        printf("Error: Entries buffer is null.\n");
        return false; // Error: Buffer is null
    }
    // Calculate the starting sector for this cluster
    unsigned int first_sector_of_cluster = ((cluster - 2) * boot_sector->sectors_per_cluster) + get_first_data_sector(boot_sector);

    for (unsigned int i = 0; i < boot_sector->sectors_per_cluster; i++) {
        // Calculate sector number to write to
        unsigned int sector_number = first_sector_of_cluster + i;
        // Calculate the pointer to the part of the entries buffer to write
        void* buffer_ptr = ((unsigned char*)entries) + (i * boot_sector->bytes_per_sector);
        
        // Write the sector
        if (!ata_write_sector(ata_base_address, sector_number, buffer_ptr, ata_is_master)) {
            printf("Error: Failed to write to sector %u.\n", sector_number);
            return false; // Error writing sector
        }
        
        // Verify the write by reading back the sector
        unsigned char verify_buffer[boot_sector->bytes_per_sector];
        if (!ata_read_sector(ata_base_address, sector_number, verify_buffer, ata_is_master)) {
            printf("Error: Failed to read back sector %u for verification\n", sector_number);
            return false;
        }
        
        // Compare the written data
        if (memcmp(buffer_ptr, verify_buffer, boot_sector->bytes_per_sector) != 0) {
            printf("Error: Write verification failed for directory cluster sector %u\n", sector_number);
            printf("       Possible disk write failure detected\n");
            return false;
        }
    }
    return true;
}
// return the start sector of a cluster
unsigned int cluster_to_sector(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    // Validate cluster number
    if (!is_valid_cluster(boot_sector, cluster)) {
        printf("Error: Invalid cluster number %u\n", cluster);
        return 0; // Return invalid sector
    }
    
    unsigned int first_data_sector = boot_sector->reserved_sector_count + (boot_sector->number_of_fats * boot_sector->fat_size_32);
    return partition_lba_offset + ((cluster - 2) * boot_sector->sectors_per_cluster) + first_data_sector;
}

void read_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster_number, void* buffer) {
    if (!is_valid_cluster(boot_sector, cluster_number)) {
        printf("Error: Cannot read invalid cluster %u\n", cluster_number);
        return;
    }
    
    unsigned int startSector = cluster_to_sector(boot_sector, cluster_number);
    if (startSector == 0) {
        printf("Error: Invalid sector for cluster %u\n", cluster_number);
        return;
    }
    
    for (unsigned int i = 0; i < boot_sector->sectors_per_cluster; ++i) {
        ata_read_sector(ata_base_address, startSector + i, buffer + (i * SECTOR_SIZE), ata_is_master);
    }
}

unsigned int read_start_cluster(struct fat32_dir_entry* entry) {
    return ((unsigned int)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

unsigned int get_next_cluster_in_chain(struct fat32_boot_sector* boot_sector, unsigned int current_cluster) {
    unsigned int fat_offset = current_cluster * 4; // 4 bytes per FAT32 entry
    unsigned int fat_sector = partition_lba_offset + boot_sector->reserved_sector_count + (fat_offset / boot_sector->bytes_per_sector);
    unsigned int ent_offset = fat_offset % boot_sector->bytes_per_sector;
    // Buffer to read a part of the FAT
    unsigned char buffer[boot_sector->bytes_per_sector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(ata_base_address, fat_sector, buffer, ata_is_master)) {
        // Handle read error
        return INVALID_CLUSTER;
    }
    // Read the 4 bytes of the current cluster's entry from the buffer
    unsigned int nextCluster = *(unsigned int*)&buffer[ent_offset];
    // Mask out the high 4 bits (reserved for FAT32)
    nextCluster &= FAT32_EOC_MAX;
    // Check for end of chain markers
    if (nextCluster >= FAT32_EOC_MIN) {
        return INVALID_CLUSTER; // End of chain
    }
    return nextCluster;
}

bool is_end_of_cluster_chain(unsigned int cluster) {
    return cluster >= FAT32_EOC_MIN && cluster <= FAT32_EOC_MAX;
}

// Function to find the next cluster given a directory name and a starting cluster
unsigned int find_next_cluster(struct fat32_boot_sector* boot_sector, const char* dir_name, unsigned int current_cluster) {
    struct fat32_dir_entry entries[SECTOR_SIZE / sizeof(struct fat32_dir_entry)];
    unsigned int nextCluster = INVALID_CLUSTER;

    do {
        unsigned int sector = cluster_to_sector(boot_sector, current_cluster);
        for (unsigned int i = 0; i < boot_sector->sectors_per_cluster; i++) {
            // Read the entire sector
            if (!ata_read_sector(ata_base_address, sector + i, entries, ata_is_master)) {
                // Handle read error
                return INVALID_CLUSTER;
            }

            // Parse entries in this sector
            for (unsigned int j = 0; j < SECTOR_SIZE / sizeof(struct fat32_dir_entry); j++) {
                struct fat32_dir_entry entry = entries[j];

                // Check for end of directory
                if (entry.name[0] == 0x00) {
                    return INVALID_CLUSTER; // No more entries, directory not found
                }
                if (entry.name[0] == 0xE5 || (entry.attr & 0x0F) == 0x0F) {
                    continue; // Skip deleted or LFN entries
                }

                // Check if this is the directory we're looking for
                if ((entry.attr & 0x10) && compare_names((const char*)entry.name, dir_name) == 0) {
                    nextCluster = ((unsigned int)entry.first_cluster_high << 16) | entry.first_cluster_low;
                    return nextCluster;
                }
            }
        }

        // Move to the next cluster in the chain
        current_cluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    } while (current_cluster < FAT32_EOC_MIN);

    return INVALID_CLUSTER; // Directory not found
}

// Function to format the filename in the 8.3 format
void format_filename(char *output, unsigned char *name) {
    // Copy the first 8 characters as the filename, trimming trailing spaces
    char filename[9] = {0};  // 8 characters + null terminator
    strncpy(filename, (const char *)name, 8);
    for (int i = 7; i >= 0; i--) {
        if (filename[i] == ' ') {
            filename[i] = '\0';  // Trim trailing spaces
        } else {
            break;
        }
    }

    // Copy the next 3 characters as the extension, trimming trailing spaces
    char extension[4] = {0};  // 3 characters + null terminator
    strncpy(extension, (const char *)(name + 8), 3);
    for (int i = 2; i >= 0; i--) {
        if (extension[i] == ' ') {
            extension[i] = '\0';  // Trim trailing spaces
        } else {
            break;
        }
    }

    // Format the output as 'FILENAME.EXT' or just 'FILENAME' if no extension
    if (strlen(extension) > 0) {
        snprintf(output, 13, "%s.%s", filename, extension);
    } else {
        snprintf(output, 9, "%s", filename);
    }
}

// Function to extract date and time from FAT32 format
void extract_fat32_date(uint16_t fat_date, int* day, int* month, int* year) {
    *day = fat_date & 0x1F;
    *month = (fat_date >> 5) & 0x0F;
    *year = ((fat_date >> 9) & 0x7F) + 1980;
}

void extract_fat32_time(uint16_t fat_time, int* hours, int* minutes, int* seconds) {
    *seconds = (fat_time & 0x1F) * 2;
    *minutes = (fat_time >> 5) & 0x3F;
    *hours = (fat_time >> 11) & 0x1F;
}

// Function to read and print directory entries in a DOS-like format
void read_cluster_dir_entries(unsigned int current_cluster) {
    unsigned int sector = cluster_to_sector(&boot_sector, current_cluster);
    struct fat32_dir_entry entries[SECTOR_SIZE * boot_sector.sectors_per_cluster / sizeof(struct fat32_dir_entry)];

    // Print DOS-like header
    printf(" Volume in drive C has no label\n");
    printf(" Directory of \\\n\n");
    printf("FILENAME      SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    // Read directory entries - read entire cluster
    for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        void* buffer_offset = (void*)((uint8_t*)entries + (i * SECTOR_SIZE));
        if (!ata_read_sector(ata_base_address, sector + i, buffer_offset, ata_is_master)) {
            printf("Error reading sector %u\n", sector + i);
            return;
        }
    }

    for (unsigned int j = 0; j < sizeof(entries) / sizeof(struct fat32_dir_entry); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            break;
        }

        // Skip LFN entries and deleted entries
        if ((entries[j].name[0] == 0xE5) || (entries[j].attr & 0x0F) == 0x0F) {
            continue;
        }

        // Process 8.3 entries and format the filename
        char current_name[13]; // Buffer to hold formatted filename
        format_filename(current_name, entries[j].name);

        int day, month, year, hours, minutes, seconds;
        extract_fat32_date(entries[j].write_date, &day, &month, &year);
        extract_fat32_time(entries[j].write_time, &hours, &minutes, &seconds);

        // Print directory or file entry in a DOS-like format
        if (entries[j].attr & 0x10) {  // Directory
            printf("%-12s   <DIR>          %02d-%02d-%04d  %02d:%02d:%02d\n",
                   current_name, day, month, year, hours, minutes, seconds);
        } else {  // File
            printf("%-12s %10u %02d-%02d-%04d  %02d:%02d:%02d\n",
                   current_name, entries[j].file_size, day, month, year, hours, minutes, seconds);
        }
    }
}

unsigned int allocate_new_cluster(struct fat32_boot_sector* boot_sector) {
    // Assuming you have functions to read and write to the FAT
    // and a function to get the total number of clusters in the filesystem
    unsigned int total_clusters = get_total_clusters(boot_sector); // Implement this function
    // Scan the FAT for a free cluster
    for (unsigned int cluster = 2; cluster < total_clusters; cluster++) {
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

bool link_cluster_to_chain(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, unsigned int new_cluster) {
    unsigned int current_cluster = parent_cluster;
    unsigned int nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);

    while (nextCluster < FAT32_EOC_MIN) {
        current_cluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    }
    // current_cluster now points to the last cluster in the chain
    // Update the FAT to link the new cluster to the chain
    if (!mark_cluster_in_fat(boot_sector, current_cluster, new_cluster)) {
        printf("Error: Failed to link cluster %u to chain starting at %u\n", new_cluster, parent_cluster);
        return false; // Failed to update the FAT
    }
    return true;
}

bool free_cluster_chain(struct fat32_boot_sector* boot_sector, unsigned int start_cluster) {
    unsigned int current_cluster = start_cluster;
    unsigned int nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);

    while (nextCluster < FAT32_EOC_MIN) {
        if (!mark_cluster_in_fat(boot_sector, current_cluster, 0)) {
            printf("Error: Failed to free cluster %u\n", current_cluster);
            return false;
        }
        current_cluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    }
    return true;
}
