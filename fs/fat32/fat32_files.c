#include "fat32.h"
#include "lib/libc/stdio.h"

// Function to read a file's data into a buffer
// void read_file_data(unsigned int start_cluster, char* buffer, unsigned int size) {
//     unsigned int current_cluster = start_cluster;
//     unsigned int bytes_read = 0;
//     while (bytes_read < size) {
//         unsigned int sector_number = cluster_to_sector(&boot_sector, current_cluster);
//         // Read each sector in the current cluster
//         for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
//             ata_read_sector(current_drive->base, sector_number + i, buffer + bytes_read, current_drive->is_master);
//             bytes_read += SECTOR_SIZE;
//             if (bytes_read >= size) {
//                 break;  // Stop if we have read the required size
//             }
//         }
//         // Get the next cluster in the chain
//         current_cluster = get_next_cluster_in_chain(&boot_sector, current_cluster);
//         // Check if we have reached the end of the file
//         if (is_end_of_cluster_chain(current_cluster)) {
//             break;
//         }
//     }
// }

unsigned int read_file_data(unsigned int start_cluster, char* buffer, unsigned int buffer_size, unsigned int bytes_to_read) {
    extern drive_t* current_drive;
    
    printf("read_file_data: start_cluster=%u, buffer_size=%u, bytes_to_read=%u\n", start_cluster, buffer_size, bytes_to_read);
    printf("  Using: base=0x%X, is_master=%d, drive=%s\n", current_drive->base, current_drive->is_master, current_drive->name);
    
    if (buffer == NULL || buffer_size == 0 || bytes_to_read == 0) {
        // Invalid parameters; return 0 to indicate no data read
        printf("read_file_data: Invalid parameters\n");
        return 0;
    }

    unsigned int current_cluster = start_cluster;
    unsigned int total_bytes_read = 0;

    printf("read_file_data: Starting read loop\n");
    while (total_bytes_read < bytes_to_read) {
        printf("read_file_data: current_cluster=%u, total_bytes_read=%u\n", current_cluster, total_bytes_read);
        unsigned int sector_number = cluster_to_sector(&boot_sector, current_cluster);
        printf("read_file_data: sector_number=%u, sectorsPerCluster=%u\n", sector_number, boot_sector.sectors_per_cluster);

        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
            printf("read_file_data: Reading sector %u of cluster\n", i);
            // Calculate the number of bytes to read in this iteration
            unsigned int bytes_remaining = bytes_to_read - total_bytes_read;
            unsigned int bytes_to_read_now = (bytes_remaining < SECTOR_SIZE) ? bytes_remaining : SECTOR_SIZE;

            // Ensure we don't read past the end of the buffer
            if (total_bytes_read + bytes_to_read_now > buffer_size) {
                bytes_to_read_now = buffer_size - total_bytes_read;
            }

            printf("read_file_data: About to call ata_read_sector\n");
            // Read data into the buffer
            ata_read_sector(current_drive->base, sector_number + i, buffer + total_bytes_read, current_drive->is_master);
            printf("read_file_data: ata_read_sector returned\n");
            total_bytes_read += bytes_to_read_now;

            // Break the loop if we have read the requested number of bytes
            if (total_bytes_read >= bytes_to_read) {
                printf("read_file_data: Reached bytes_to_read, breaking\n");
                break;
            }
        }

        printf("read_file_data: Getting next cluster\n");
        // Get the next cluster in the chain
        current_cluster = get_next_cluster_in_chain(&boot_sector, current_cluster);
        printf("read_file_data: Next cluster = %u\n", current_cluster);

        // Check if we have reached the end of the file or if an invalid cluster is encountered
        if (is_end_of_cluster_chain(current_cluster) || current_cluster == INVALID_CLUSTER) {
            printf("read_file_data: End of cluster chain\n");
            break;
        }
    }

    printf("read_file_data: Finished, total_bytes_read=%u\n", total_bytes_read);
    // Return the total number of bytes read
    return total_bytes_read;
}

int read_file_data_to_address(unsigned int start_cluster, void* load_address, unsigned int file_size) {
    // Safety checks
    if (file_size == 0) {
        return 0; // Empty file
    }
    
    if (boot_sector.sectors_per_cluster == 0) {
        printf("Error: sectors_per_cluster is zero\n");
        return 0;
    }
    
    unsigned int current_cluster = start_cluster;
    unsigned int bytes_read = 0;
    unsigned char* buffer_ptr = (unsigned char*)load_address;
    
    while (bytes_read < file_size) {
        // Validate cluster before using it
        extern bool is_valid_cluster(struct fat32_boot_sector* bs, unsigned int cluster);
        if (!is_valid_cluster(&boot_sector, current_cluster)) {
            printf("Error: Invalid cluster %u during file read\n", current_cluster);
            break;
        }
        
        unsigned int sector_number = cluster_to_sector(&boot_sector, current_cluster);
        
        // Read each sector in the current cluster
        for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (!ata_read_sector(current_drive->base, sector_number + i, buffer_ptr, current_drive->is_master)) {
                printf("Error: Failed to read sector %u\n", sector_number + i);
                return bytes_read;
            }
            
            buffer_ptr += SECTOR_SIZE;
            bytes_read += SECTOR_SIZE;

            if (bytes_read >= file_size) {
                break; // Stop if we have read the entire file
            }
        }
        
        // Get the next cluster in the chain
        current_cluster = get_next_cluster_in_chain(&boot_sector, current_cluster);
        
        // Check if we have reached the end of the file
        if (is_end_of_cluster_chain(current_cluster)) {
            break;
        }
    }
    
    return bytes_read;
}

int fat32_load_file(const char* filename, void* load_address) {
    // Safety check: ensure boot sector is initialized
    if (boot_sector.bytes_per_sector == 0 || boot_sector.sectors_per_cluster == 0) {
        printf("Error: Filesystem not properly initialized\n");
        return 0;
    }
    
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL) {
        printf("File %s not found for loading into buffer.\n", filename);
        return 0; // we return 0 if the file was not found which is the size of the file
    }
    
    unsigned int file_size = entry->file_size;
    unsigned int start_cluster = read_start_cluster(entry);
    
    // Safety check: validate cluster number
    if (start_cluster < 2) {
        printf("Error: Invalid start cluster %u\n", start_cluster);
        free(entry);
        return 0;
    }
    
    // Load the file
    int result = read_file_data_to_address(start_cluster, load_address, file_size);
    
    // Free the directory entry
    free(entry);
    
    return result;
}

// void openAndLoadFile(const char* filename) {
//     struct fat32_dir_entry* entry = find_file_in_directory(filename);
//     if (entry == NULL) {
//         printf("File not found.\n");
//         return;
//     }
//     unsigned int start_cluster = read_start_cluster(entry);
//     int file_size = entry->file_size;
//     char* buffer = malloc(file_size);
//     if (buffer == NULL) {
//         printf("Not enough memory.\n");
//         return;
//     }
//     read_file_data(start_cluster, buffer, sizeof(buffer), file_size);

//     // Process the file data in bufferb
//     // for(int i = 0; i < file_size; i++){
//     //     printf("%c", buffer[i]);
//     // }
//     free(entry); // Free the memory after use
//     free(buffer); // Free the memory after use
// }

// Function to find a file in the current directory
struct fat32_dir_entry* find_file_in_directory(const char* filename) {
    extern drive_t* current_drive;
    
    printf("find_file_in_directory: Looking for '%s'\n", filename);
    printf("  Using: base=0x%X, is_master=%d, cluster=%u, drive=%s\n", 
           current_drive->base, current_drive->is_master, current_directory_cluster, current_drive->name);
    
    unsigned int sector = cluster_to_sector(&boot_sector, current_directory_cluster);
    printf("  Calculated sector: %u\n", sector);
    struct fat32_dir_entry* entries = (struct fat32_dir_entry*)malloc(SECTOR_SIZE * boot_sector.sectors_per_cluster / sizeof(struct fat32_dir_entry));

    if (entries == NULL) {
        // Handle memory allocation failure
        printf("Error: Failed to allocate memory for directory entries.\n");
        return NULL;
    }
    for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        if (!ata_read_sector(current_drive->base, sector + i, &entries[i * (SECTOR_SIZE / sizeof(struct fat32_dir_entry))], current_drive->is_master)) {
            // Handle read error
            free(entries);
            return NULL;
        }
    }

    for (unsigned int j = 0; j < boot_sector.sectors_per_cluster * (SECTOR_SIZE / sizeof(struct fat32_dir_entry)); j++) {
        if (entries[j].name[0] == 0x00) { // End of directory
            printf("  End of directory at entry %u\n", j);
            break;
        }

        if (entries[j].name[0] == 0xE5 || (entries[j].attr & 0x0F) == 0x0F) { // Deleted or LFN entry
            continue;
        }

        // Debug: show the raw FAT32 name
        char debug_name[12];
        for (int k = 0; k < 11; k++) {
            debug_name[k] = entries[j].name[k];
        }
        debug_name[11] = '\0';
        printf("  Comparing FAT32 name '%.11s' with '%s'\n", debug_name, filename);
        
        // Use the proper compare_names function that handles FAT32 8.3 format correctly
        if (compare_names((const char*)entries[j].name, filename) == 0) {
            printf("  MATCH FOUND!\n");
            struct fat32_dir_entry* found_entry = (struct fat32_dir_entry*)malloc(sizeof(struct fat32_dir_entry));
            if (found_entry == NULL) {
                // Handle memory allocation failure
                printf("Error: Failed to allocate memory for directory entry.\n");
                free(entries);
                return NULL;
            }

            memcpy(found_entry, &entries[j], sizeof(struct fat32_dir_entry));
            free(entries); // Free the allocated memory
            return found_entry; // File found
        }
    }

    printf("  File '%s' not found in directory\n", filename);
    free(entries); // Free the allocated memory
    return NULL; // File not found
}

bool fat32_create_file(const char* filename) {
    // 1. Find a free cluster for the new file
    unsigned int new_file_cluster = find_free_cluster(&boot_sector);
    if (new_file_cluster == INVALID_CLUSTER) {
        printf("Failed to allocate a new cluster for the file.\n");
        return false;
    }
    
    // 2. Update the FAT for the new file cluster
    if (!mark_cluster_in_fat(&boot_sector, new_file_cluster, FAT32_EOC_MAX)) {
        printf("Failed to update the FAT for the new file cluster.\n");
        // Rollback not needed - cluster is still marked as free
        return false;
    }
    
    // 3. Add a directory entry for the new file in the current directory
    if (!add_entry_to_directory(&boot_sector, current_directory_cluster, filename, new_file_cluster, 0)) { // 0 for normal file attributes
        printf("Failed to add a directory entry for the new file.\n");
        // ROLLBACK: Free the cluster we just allocated
        printf("Rolling back: Freeing allocated cluster %u\n", new_file_cluster);
        mark_cluster_in_fat(&boot_sector, new_file_cluster, 0);  // Mark as free
        return false;
    }
    
    printf("File '%s' created successfully at cluster %u\n", filename, new_file_cluster);
    
    // Sync FSInfo after file creation
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

bool fat32_delete_file(const char* filename) {
    // 1. Find the directory entry for the file to delete
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return false;
    }
    
    // Save cluster info before freeing entry
    unsigned int start_cluster = read_start_cluster(entry);
    
    // 2. Free the file's cluster chain in the FAT
    if (!free_cluster_chain(&boot_sector, start_cluster)) {
        printf("Failed to free the file's cluster chain.\n");
        free(entry);  // Free allocated memory
        return false;
    }
    // 3. Remove the directory entry from the parent directory
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster, entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        free(entry);  // Free allocated memory
        return false;
    }
    
    free(entry);  // Free allocated memory
    
    // Sync FSInfo after file deletion
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

// Function to open a file and return a pointer to the file data
FILE* fat32_open_file(const char* filename, const char* mode) {
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return NULL;
    }

    unsigned int start_cluster = read_start_cluster(entry);
    int file_size = entry->file_size;

    FILE* file = (FILE*)malloc(sizeof(FILE));
    if (file == NULL) {
        printf("Not enough memory.\n");
        return NULL;
    }

    file->position = 0;
    file->size = file_size;
    file->ptr = (unsigned char*)malloc(file_size);
    file->mode = mode;
    file->name = filename;
    file->start_cluster = start_cluster;

    return file;
}

// read file
int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read) {
    if (strcmp(file->mode, "w") == 0) {
        printf("Error: File is not open for reading.\n");
        return 0;
    }

    if (file->position + bytes_to_read > file->size) {
        bytes_to_read = file->size - file->position;
    }

    return read_file_data(file->start_cluster, (char*)buffer, buffer_size, bytes_to_read);
}
