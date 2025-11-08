#include "fat12.h"
#include "drivers/block/fdd.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

// Additional constants
#define MAX_PATH_LENGTH 256


// Global structures and buffers
fat12_t* fat12 = NULL;
directory_entry* entries = NULL;
directory_entry* current_dir = NULL;
uint8_t* buffer = NULL;
uint8_t current_fdd_drive = 0;

// Helper: read a single sector using DMA first, then fall back to no-DMA path
static bool fdc_read_with_fallback(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* out_buf) {
    if (fdc_read_sector(drive, head, track, sector, out_buf)) {
        return true;
    }
    printf("fdc_read_with_fallback: DMA read failed for %d/%d/%d, trying no-DMA fallback\n", track, head, sector);
    if (fdc_read_sector_no_dma(drive, head, track, sector, out_buf)) {
        return true;
    }
    printf("fdc_read_with_fallback: no-DMA fallback also failed for %d/%d/%d\n", track, head, sector);
    return false;
}

// Function to calculate CHS from a logical sector number using boot sector geometry
void logical_to_chs(int logical_sector, int* track, int* head, int* sector) {
    uint16_t spt = FAT12_DEFAULT_SPT;
    uint16_t heads = FAT12_DEFAULT_HEADS;
    
    // Use boot sector values if available
    if (fat12 && fat12->boot_sector.sectors_per_track > 0 && fat12->boot_sector.heads > 0) {
        spt = fat12->boot_sector.sectors_per_track;
        heads = fat12->boot_sector.heads;
    }
    
    *track = logical_sector / (spt * heads);
    *head = (logical_sector / spt) % heads;
    *sector = (logical_sector % spt) + 1;  // Sectors are 1-based
    
    // Validate calculated values
    if (*sector < 1 || *sector > spt) {
        printf("WARNING: Invalid sector calculated: %d (max %d)\n", *sector, spt);
    }
}

// Validate cluster number is in valid FAT12 range
bool is_valid_cluster_fat12(int cluster) {
    return cluster >= FAT12_MIN_CLUSTER && cluster <= FAT12_MAX_CLUSTER;
}

// Check if cluster marks end of chain
bool is_end_of_chain_fat12(int cluster) {
    return cluster >= FAT12_EOC_MIN && cluster <= FAT12_EOC_MAX;
}

// Validate FAT12 boot sector
bool validate_fat12_boot_sector(fat12_boot_sector* bs) {
    if (!bs) {
        printf("ERROR: NULL boot sector pointer\n");
        return false;
    }
    
    // Check boot signature
    if (bs->boot_sector_signature != FAT12_BOOT_SIGNATURE) {
        printf("ERROR: Invalid boot signature: 0x%04X (expected 0x%04X)\n", 
               bs->boot_sector_signature, FAT12_BOOT_SIGNATURE);
        return false;
    }
    
    // Validate bytes per sector
    if (bs->bytes_per_sector != 512 && bs->bytes_per_sector != 1024 && 
        bs->bytes_per_sector != 2048 && bs->bytes_per_sector != 4096) {
        printf("ERROR: Invalid bytes_per_sector: %d\n", bs->bytes_per_sector);
        return false;
    }
    
    // Validate sectors per cluster (must be power of 2, max 128)
    if (bs->sectors_per_cluster == 0 || bs->sectors_per_cluster > 128 ||
        (bs->sectors_per_cluster & (bs->sectors_per_cluster - 1)) != 0) {
        printf("ERROR: Invalid sectors_per_cluster: %d\n", bs->sectors_per_cluster);
        return false;
    }
    
    // Validate FAT count
    if (bs->fat_count < 1 || bs->fat_count > 2) {
        printf("ERROR: Invalid FAT count: %d\n", bs->fat_count);
        return false;
    }
    
    // Validate root entry count
    if (bs->root_entry_count == 0) {
        printf("ERROR: Root entry count is zero\n");
        return false;
    }
    
    // Calculate total clusters to verify it's FAT12
    uint32_t total_sectors = bs->total_sectors ? bs->total_sectors : bs->total_sectors_large;
    uint32_t root_dir_sectors = ((bs->root_entry_count * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
    uint32_t data_sectors = total_sectors - (bs->reserved_sectors + 
                           (bs->fat_count * bs->sectors_per_fat) + root_dir_sectors);
    uint32_t total_clusters = data_sectors / bs->sectors_per_cluster;
    
    if (total_clusters >= 4085) {
        printf("ERROR: Too many clusters for FAT12: %u (max 4084)\n", total_clusters);
        return false;
    }
    
    if (total_clusters < 2) {
        printf("ERROR: Too few clusters: %u\n", total_clusters);
        return false;
    }
    
    printf("FAT12 validation passed: %u clusters\n", total_clusters);
    return true;
}

// Valid filename check
bool is_valid_filename(const char* filename, size_t length) {
    const char* valid_chars = "!#$%&'()-@^_`{}~";
    for (size_t i = 0; i < length; i++) {
        if (!isalnum(filename[i]) && !strchr(valid_chars, filename[i])) {
            return false;
        }
    }
    return true;
}

// Read the FAT table and initialize fat12 structure
int read_fat12(uint8_t drive, fat12_t* fat12) {
    if (fat12->boot_sector.boot_sector_signature == 0xAA55) {
        printf("fat12 already initialized.\n");
        return true;
    }

    buffer = (uint8_t*)malloc(FAT12_SECTOR_SIZE);
    if (!buffer) {
        printf("Memory allocation failed for boot sector buffer.\n");
        return false;
    }

    printf("Attempting to read boot sector from drive %d (H:0, C:0, S:1)...\n", drive);
    if (!fdc_read_with_fallback(drive, 0, 0, 1, buffer)) {
        printf("Error reading boot sector from FDC (both DMA and no-DMA failed).\n");
        free(buffer);
        return false;
    }

    printf("Boot sector read successful. First 16 bytes:\n");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
    
    printf("Boot signature bytes at offset 510-511: 0x%02X%02X\n", 
           buffer[510], buffer[511]);

    memcpy(&fat12->boot_sector, buffer, sizeof(fat12_boot_sector));
    free(buffer);

    // Validate boot sector
    if (!validate_fat12_boot_sector(&fat12->boot_sector)) {
        printf("Boot sector validation failed.\n");
        return false;
    }

    // Debug: Show boot sector geometry
    printf("Boot sector geometry:\n");
    printf("  bytes_per_sector: %u\n", fat12->boot_sector.bytes_per_sector);
    printf("  sectors_per_track: %u\n", fat12->boot_sector.sectors_per_track);
    printf("  heads: %u\n", fat12->boot_sector.heads);
    printf("  reserved_sectors: %u\n", fat12->boot_sector.reserved_sectors);
    printf("  fat_count: %u\n", fat12->boot_sector.fat_count);
    printf("  sectors_per_fat: %u\n", fat12->boot_sector.sectors_per_fat);
    printf("  root_entry_count: %u\n", fat12->boot_sector.root_entry_count);
    
    fat12->fat_start = fat12->boot_sector.reserved_sectors;
    fat12->root_dir_start = fat12->fat_start + (fat12->boot_sector.fat_count * fat12->boot_sector.sectors_per_fat);
    fat12->data_start = fat12->root_dir_start + (fat12->boot_sector.root_entry_count * FAT12_ROOT_ENTRY_SIZE / FAT12_SECTOR_SIZE);
    
    printf("Calculated sectors:\n");
    printf("  fat_start: %d\n", fat12->fat_start);
    printf("  root_dir_start: %d\n", fat12->root_dir_start);
    printf("  data_start: %d\n", fat12->data_start);
    
    // Allocate and load FAT table into memory
    uint32_t fat_size = fat12->boot_sector.sectors_per_fat * FAT12_SECTOR_SIZE;
    fat12->fat = (uint8_t*)malloc(fat_size);
    if (!fat12->fat) {
        printf("ERROR: Failed to allocate memory for FAT table (%u bytes)\n", fat_size);
        return false;
    }
    
    // Read FAT table sectors
    printf("Loading FAT table (%u sectors, %u bytes)...\n", 
           fat12->boot_sector.sectors_per_fat, fat_size);
    
        for (int i = 0; i < fat12->boot_sector.sectors_per_fat; i++) {
        int logical_sector = fat12->fat_start + i;
        int track, head, sector;
        logical_to_chs(logical_sector, &track, &head, &sector);
        
        uint8_t* fat_buffer = fat12->fat + (i * FAT12_SECTOR_SIZE);
        if (!fdc_read_with_fallback(drive, head, track, sector, fat_buffer)) {
            printf("ERROR: Failed to read FAT sector %d (both DMA and no-DMA)\n", i);
            free(fat12->fat);
            fat12->fat = NULL;
            return false;
        }
    }
    
    printf("FAT table loaded successfully\n");

    printf("fat12 initialized: FAT Start Sector: %d, Root Directory Start Sector: %d, Data Region Start Sector: %d\n", 
           fat12->fat_start, fat12->root_dir_start, fat12->data_start);
    return true;
}

// Initialize fat12 and load root directory
bool fat12_init_fs(uint8_t drive) {
    current_fdd_drive = drive; // Set the current drive
    
    printf("fat12_init_fs: Attempting to allocate %d bytes for fat12_t\n", sizeof(fat12_t));
    
    // Free existing allocation if any
    if (fat12 != NULL) {
        printf("Freeing existing fat12 structure at %p\n", fat12);
        if (fat12->fat != NULL) {
            free(fat12->fat);
            fat12->fat = NULL;
        }
        free(fat12);
        fat12 = NULL;
    }
    
    // Initialize fat12 structure
    fat12 = (fat12_t*)malloc(sizeof(fat12_t));
    if (!fat12) {
        printf("Memory allocation failed for fat12 structure.\n");
        printf("Requested size: %d bytes\n", sizeof(fat12_t));
        return false;
    }
    printf("fat12 structure allocated at %p\n", fat12);
    memset(fat12, 0, sizeof(fat12_t));

    if (!read_fat12(drive, fat12)) {
        printf("Failed to read boot sector for FAT12.\n");
        free(fat12);
        fat12 = NULL;
        return false;
    }
    return true;
}

// Function to calculate and fetch the next cluster in fat12
int get_next_cluster(int current_cluster) {
    // Validate inputs
    if (!fat12) {
        printf("ERROR: FAT12 not initialized\n");
        return -1;
    }
    
    if (!fat12->fat) {
        printf("ERROR: FAT table not loaded\n");
        return -1;
    }
    
    if (!is_valid_cluster_fat12(current_cluster)) {
        printf("ERROR: Invalid cluster number: %d\n", current_cluster);
        return -1;
    }
    
    // Calculate offset in FAT table (FAT12 uses 12 bits per entry)
    int offset = (current_cluster * 3) / 2;
    
    // Bounds check
    uint32_t fat_size = fat12->boot_sector.sectors_per_fat * FAT12_SECTOR_SIZE;
    if (offset + 1 >= fat_size) {
        printf("ERROR: FAT offset out of bounds: %d (max %u)\n", offset, fat_size);
        return -1;
    }
    
    // Read 12-bit FAT entry (even/odd cluster handling)
    uint16_t next_cluster;
    if (offset % 2 == 0) {
        // Even cluster: use low 12 bits
        next_cluster = (fat12->fat[offset] | (fat12->fat[offset + 1] << 8)) & 0x0FFF;
    } else {
        // Odd cluster: use high 12 bits
        next_cluster = ((fat12->fat[offset] >> 4) | (fat12->fat[offset + 1] << 4)) & 0x0FFF;
    }
    
    // Check for end of chain or bad cluster
    if (is_end_of_chain_fat12(next_cluster)) {
        return -1;  // End of cluster chain
    }
    
    if (next_cluster == FAT12_BAD_CLUSTER) {
        printf("ERROR: Bad cluster encountered: 0x%03X\n", next_cluster);
        return -1;
    }
    
    if (next_cluster == FAT12_FREE_CLUSTER || next_cluster == FAT12_RESERVED_CLUSTER) {
        printf("ERROR: Invalid cluster in chain: 0x%03X\n", next_cluster);
        return -1;
    }
    
    return next_cluster;
}

// Function to extract date from fat12 format
void extract_date(uint16_t fat_date, int* day, int* month, int* year) {
    *day = fat_date & 0x1F;
    *month = (fat_date >> 5) & 0x0F;
    *year = ((fat_date >> 9) & 0x7F) + 1980;
}

// Function to extract time from fat12 format
void extract_time(uint16_t fat_time, int* hours, int* minutes, int* seconds) {
    *seconds = (fat_time & 0x1F) * 2;
    *minutes = (fat_time >> 5) & 0x3F;
    *hours = (fat_time >> 11) & 0x1F;
}

// Read directory entries (root or subdirectory)
int fat12_read_dir_entries(directory_entry* dir) {
    int entries_found = 0;
    uint8_t* local_buffer = NULL;  // Use local buffer instead of global
    
    // Free existing entries allocation if any
    if (entries != NULL) {
        free(entries);
        entries = NULL;
    }
    
    entries = (directory_entry*)malloc(FAT12_MAX_ROOT_ENTRIES * sizeof(directory_entry));
    if (!entries) {
        printf("Memory allocation failed for directory entries.\n");
        return -1;
    }

    local_buffer = (uint8_t*)malloc(FAT12_SECTOR_SIZE * FAT12_ROOT_DIR_SECTORS);  // Allocate buffer for multiple sectors
    if (!local_buffer) {
        printf("Memory allocation failed for sector buffer.\n");
        free(entries);
        entries = NULL;
        return -1;
    }

    if (dir == NULL) {
        printf("Reading root directory entries.\n");

        int logical_sector = fat12->root_dir_start;
        int track, head, sector;
        logical_to_chs(logical_sector, &track, &head, &sector);

        // Read multiple sectors at once (use per-sector fallback)
        for (int si = 0; si < FAT12_ROOT_DIR_SECTORS; si++) {
            int ls = logical_sector + si;
            int t, h, s;
            logical_to_chs(ls, &t, &h, &s);
            uint8_t* dest = local_buffer + (si * FAT12_SECTOR_SIZE);
            if (!fdc_read_with_fallback(current_fdd_drive, h, t, s, dest)) {
                printf("Error reading root directory sector %d (logical %d).\n", si, ls);
                free(entries);
                free(local_buffer);
                return -1;
            }
        }

        // Copy entries from the buffer
        for (int i = 0; i < FAT12_ROOT_DIR_SECTORS && entries_found < FAT12_MAX_ROOT_ENTRIES; i++) {
            memcpy(&entries[entries_found], local_buffer + (i * FAT12_SECTOR_SIZE), FAT12_SECTOR_SIZE);
            entries_found += FAT12_SECTOR_SIZE / FAT12_ROOT_ENTRY_SIZE;
        }
    } else {
        // Read subdirectory starting from its cluster
        int cluster = dir->first_cluster_low;
        printf("Reading subdirectory. Start cluster: %d\n", cluster);

        while (cluster >= FAT12_MIN_CLUSTER && cluster < FAT12_MAX_CLUSTER && entries_found < FAT12_MAX_ROOT_ENTRIES) {
            int start_sector = fat12->data_start + (cluster - 2) * fat12->boot_sector.sectors_per_cluster;
            int track, head, sector;
            logical_to_chs(start_sector, &track, &head, &sector);

            // Read multiple sectors for the cluster
            // Read each sector of the cluster with fallback
            for (int si = 0; si < fat12->boot_sector.sectors_per_cluster; si++) {
                int ls = (fat12->data_start + (cluster - 2) * fat12->boot_sector.sectors_per_cluster) + si;
                int t, h, s;
                logical_to_chs(ls, &t, &h, &s);
                uint8_t* dest = local_buffer + (si * FAT12_SECTOR_SIZE);
                if (!fdc_read_with_fallback(current_fdd_drive, h, t, s, dest)) {
                    printf("Error reading subdirectory sector %d (logical %d).\n", si, ls);
                    free(entries);
                    free(local_buffer);
                    return -1;
                }
            }

            // Copy entries from the buffer
            for (int i = 0; i < fat12->boot_sector.sectors_per_cluster && entries_found < FAT12_MAX_ROOT_ENTRIES; i++) {
                memcpy(&entries[entries_found], local_buffer + (i * FAT12_SECTOR_SIZE), FAT12_SECTOR_SIZE);
                entries_found += FAT12_SECTOR_SIZE / FAT12_ROOT_ENTRY_SIZE;
            }

            cluster = get_next_cluster(cluster);  // Move to the next cluster in the chain
            printf("Next cluster: %d\n", cluster);
        }
    }

    printf("Entries found: %d\n", entries_found);

    free(local_buffer);
    return entries_found;
}


// Print directory entries in DOS-like format
void print_dir_entries(directory_entry* dir, int entries_found) {
    printf(" Volume in drive A has no label\n");
    printf(" Directory of %s\n\n", dir == NULL ? "\\" : *dir->filename == 0 ? "\\" : (char*)dir->filename);
    printf("FILENAME   EXT    SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < entries_found; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & (0x10 | 0x20))) continue;

        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)entry->filename, 8);
        strncpy(extension, (const char*)entry->extension, 3);

        int day, month, year, hours, minutes, seconds;
        extract_date(entry->last_write_date, &day, &month, &year);
        extract_time(entry->last_write_time, &hours, &minutes, &seconds);

        if (entry->attributes & 0x10) {  // Directory
            printf("%-8s   %-3s   <DIR>    %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, day, month, year, hours, minutes, seconds);
        } else {  // File
            printf("%-8s   %-3s   %8u  %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, entry->file_size, day, month, year, hours, minutes, seconds);
        }
    }
    printf("\n");
}

// Change to a new directory if it exists
bool fat12_change_directory(const char* relative_path) {
    int num_entries = fat12_read_dir_entries(current_dir);
    if (num_entries < 0) {
        printf("Failed to read directory.\n");
        return false;
    }

    for (int i = 0; i < num_entries; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & 0x10)) continue;  // Only directories

        char entry_name[9] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        entry_name[8] = '\0';  // Ensure null-termination

        char trimmed_name[9] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);

        if (strcmp(trimmed_name, relative_path) == 0) {
            // set trimmed_name to entry 
            str_trim_spaces(trimmed_name, (char*)entry->filename, 8);

            current_dir = entry;
            printf("Changed directory to %s\n", relative_path);
            return true;
        }
    }
    printf("Directory not found: %s\n", relative_path);
    return false;
}

// Open a file in the current directory
fat12_file* fat12_open_file(const char* filename, const char* mode) {
    // Validate inputs
    if (!filename || !mode) {
        printf("ERROR: NULL filename or mode\n");
        return NULL;
    }
    
    if (!fat12 || !fat12->fat) {
        printf("ERROR: FAT12 not initialized\n");
        return NULL;
    }
    
    // Validate filename length
    size_t filename_len = strlen(filename);
    if (filename_len == 0 || filename_len > 12) {  // 8.3 format max
        printf("ERROR: Invalid filename length: %zu\n", filename_len);
        return NULL;
    }
    
    int num_entries = fat12_read_dir_entries(current_dir);
    if (num_entries < 0) {
        printf("Failed to read directory contents.\n");
        return NULL;
    }

    directory_entry* file_entry = NULL;
    for (int i = 0; i < num_entries; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x10) continue;  // Skip directories

        char entry_name[9] = {0};
        char entry_ext[4] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        strncpy(entry_ext, (const char*)entry->extension, 3);

        char trimmed_name[13] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);

        if (entry_ext[0] != ' ') {
            strncat(trimmed_name, ".", 1);
            strncat(trimmed_name, entry_ext, 3);
        }

        if (strcmp(trimmed_name, filename) == 0) {
            file_entry = entry;
            break;
        }
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return NULL;
    }
    
    // Validate file entry
    if (!is_valid_cluster_fat12(file_entry->first_cluster_low)) {
        printf("ERROR: File has invalid cluster: %u\n", file_entry->first_cluster_low);
        return NULL;
    }
    
    if (file_entry->file_size == 0) {
        printf("WARNING: File size is zero: %s\n", filename);
    }

    fat12_file* file = (fat12_file*)malloc(sizeof(fat12_file));
    if (file == NULL) {
        printf("Failed to allocate memory for file structure.\n");
        return NULL;
    }
    
    // Initialize all fields
    memset(file, 0, sizeof(fat12_file));

    file->base = (unsigned char*)malloc(file_entry->file_size > 0 ? file_entry->file_size : 1);
    if (file->base == NULL) {
        printf("Failed to allocate memory for file buffer.\n");
        free(file);
        return NULL;
    }

    file->ptr = file->base;
    file->start_cluster = file_entry->first_cluster_low;
    file->size = file_entry->file_size;
    file->position = 0;
    strncpy((char*)file->name, filename, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->mode = mode;

    printf("File opened: %s, Size: %d bytes\n", file->name, file->size);
    return file;
}

// Print the contents of a file
void print_file_content(fat12_file* file) {
    if (file == NULL) {
        printf("Invalid file handle.\n");
        return;
    }

    // Calculate the size of the buffer based on the size of the file
    size_t buffer_size = file->size; // Use the file size as the buffer size directly

    // Allocate the buffer
    char* buffer = (char*)malloc(sizeof(char) * buffer_size);

    if (buffer == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }
    memset(buffer, 0, buffer_size);

    int bytes_read = fat12_read_file(file, buffer, buffer_size, file->size);
    if (bytes_read > 0) {
        printf("File contents:\n%s\n", buffer);
        hex_dump((unsigned char*)buffer, buffer_size);
    } else {
        printf("Failed to read file content.\n");
    }

    free(buffer);
}

// Read directory based on the specified path
bool fat12_read_dir(const char* path) {
    printf("-----Reading directory: %s-----\n", path ? path : "(current directory)");

    int num_entries = 0;
    if(current_dir == NULL) {
        // read root directory
        num_entries = fat12_read_dir_entries(NULL);
        if (num_entries < 0) {
            printf("Failed to load root directory.\n");
            return false;
        }
    }else {
        // read subdirectory
        num_entries = fat12_read_dir_entries(current_dir);
        if (num_entries < 0) {
            printf("Failed to load subdirectory.\n");
            return false;
        }
    }

    print_dir_entries(current_dir, num_entries);
    return true;
}

int fat12_read_file(fat12_file* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read) {
    // Validate inputs
    if (file == NULL || buffer == NULL) {
        printf("ERROR: Invalid file handle or buffer.\n");
        return 0;
    }
    
    if (!fat12 || !fat12->fat) {
        printf("ERROR: FAT12 not initialized or FAT table not loaded.\n");
        return 0;
    }
    
    if (buffer_size == 0 || bytes_to_read == 0) {
        return 0;
    }
    
    if (!is_valid_cluster_fat12(file->start_cluster)) {
        printf("ERROR: File has invalid start cluster: %u\n", file->start_cluster);
        return 0;
    }

    // Ensure we don't read beyond the end of the file or buffer
    if (file->position + bytes_to_read > file->size) {
        bytes_to_read = file->size - file->position;
    }
    if (bytes_to_read > buffer_size) {
        bytes_to_read = buffer_size;
    }
    
    if (bytes_to_read == 0) {
        return 0;  // Nothing to read
    }

    unsigned int bytes_read = 0;
    unsigned int current_cluster = file->start_cluster;
    unsigned int clusterSize = FAT12_SECTOR_SIZE * fat12->boot_sector.sectors_per_cluster;

    // Calculate initial offset within the first cluster if the position is not at the start
    unsigned int startOffset = file->position % clusterSize;

    unsigned char* sectorBuffer = (unsigned char*)malloc(FAT12_SECTOR_SIZE);
    if (sectorBuffer == NULL) {
        printf("Memory allocation failed for sector buffer.\n");
        return 0;
    }

    // Read loop
    while (bytes_read < bytes_to_read && current_cluster >= FAT12_MIN_CLUSTER && current_cluster < FAT12_MAX_CLUSTER) {
        // Calculate the first sector of the current cluster
        unsigned int firstSectorOfCluster = fat12->data_start + (current_cluster - 2) * fat12->boot_sector.sectors_per_cluster;

        // Read each sector in the cluster
        for (unsigned int i = 0; i < fat12->boot_sector.sectors_per_cluster && bytes_read < bytes_to_read; i++) {
            unsigned int logical_sector = firstSectorOfCluster + i;
            int track, head, sector;
            logical_to_chs(logical_sector, &track, &head, &sector);

            // Read the sector
            if (!fdc_read_with_fallback(current_fdd_drive, head, track, sector, sectorBuffer)) {
                printf("Error reading file sector at track %d, head %d, sector %d (both DMA and no-DMA failed).\n", track, head, sector);
                free(sectorBuffer);
                return bytes_read; // Return bytes read so far on failure
            }

            // Calculate how many bytes to copy from the sector
            unsigned int offset = (bytes_read == 0) ? startOffset : 0;
            unsigned int remaining = FAT12_SECTOR_SIZE - offset;
            unsigned int bytes_to_copy = (bytes_to_read - bytes_read < remaining) ? (bytes_to_read - bytes_read) : remaining;

            // Copy the bytes from the sector buffer to the main buffer
            memcpy((unsigned char*)buffer + bytes_read, sectorBuffer + offset, bytes_to_copy);
            bytes_read += bytes_to_copy;
            file->position += bytes_to_copy;

            if (bytes_read >= bytes_to_read) {
                break;
            }
        }

        // Move to the next cluster in the chain
        {
            int next = get_next_cluster((int)current_cluster);
            if (next < 0) {
                // end of chain or error
                break;
            }
            current_cluster = (unsigned int)next;
        }
    }

    // Null-terminate the buffer if applicable (only for text data)
    if (bytes_read < buffer_size) {
        ((char*)buffer)[bytes_read] = '\0';
    }

    free(sectorBuffer);
    printf("Completed reading %u bytes from file %s into buffer.\n", bytes_read, file->name);

    return bytes_read;
}

// Close a file and free its resources
void fat12_close_file(fat12_file* file) {
    if (file) {
        if (file->base) {
            free(file->base);
            file->base = NULL;
        }
        free(file);
    }
}

// Cleanup FAT12 filesystem and free all resources
void fat12_cleanup(void) {
    if (fat12) {
        if (fat12->fat) {
            free(fat12->fat);
            fat12->fat = NULL;
        }
        free(fat12);
        fat12 = NULL;
    }
    
    if (entries) {
        free(entries);
        entries = NULL;
    }
    
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    
    current_dir = NULL;
    printf("FAT12 cleanup complete\n");
}
