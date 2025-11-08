#include "filesystem.h"
#include "drivers/block/ata.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "fs/fat32/fat32.h"
#include "fs/fat12/fat12.h"
#include "drivers/char/io.h"
#include <stddef.h>


// initialize the fat32 filesystem class
fat32_class_t fat32;

void ctor_fat32_class(fat32_class_t* fat32) {
    // Cluster and Sector Operations
    fat32->read_cluster = read_cluster;
    fat32->cluster_to_sector = cluster_to_sector;
    fat32->get_entries_per_cluster = get_entries_per_cluster;
    fat32->get_total_clusters = get_total_clusters;
    fat32->get_first_data_sector = get_first_data_sector;

    // FAT Table Operations
    fat32->read_fat_entry = read_fat_entry;
    fat32->write_fat_entry = write_fat_entry;
    fat32->mark_cluster_in_fat = mark_cluster_in_fat;
    fat32->link_cluster_to_chain = link_cluster_to_chain;
    fat32->free_cluster_chain = free_cluster_chain;
    fat32->find_free_cluster = find_free_cluster;
    fat32->allocate_new_cluster = allocate_new_cluster;
    fat32->get_next_cluster_in_chain = get_next_cluster_in_chain;
    fat32->is_end_of_cluster_chain = is_end_of_cluster_chain;

    // Directory and Entry Management
    fat32->initialize_new_directory_entries = initialize_new_directory_entries;
    fat32->create_directory_entry = create_directory_entry;
    fat32->add_entry_to_directory = add_entry_to_directory;
    fat32->remove_entry_from_directory = remove_entry_from_directory;
    fat32->find_next_cluster = find_next_cluster;
    fat32->read_cluster_dir_entries = read_cluster_dir_entries;
    fat32->write_cluster = write_cluster;
    fat32->read_start_cluster = read_start_cluster;
    fat32->find_file_in_directory = find_file_in_directory;
    fat32->fat32_change_directory = fat32_change_directory;

    // File and Data Management
    fat32->fat32_load_file = fat32_load_file;

    // Formatting and Utility Functions
    fat32->format_filename = format_filename;
    fat32->convert_to_83_format = convert_to_83_format;
    fat32->compare_names = compare_names;
    fat32->set_fat32_time = set_fat32_time;

    // Public functions
    fat32->fat32_init_fs = fat32_init_fs;

    // Directory operations
    fat32->fat32_read_dir = fat32_read_dir;
    fat32->fat32_create_dir = fat32_create_dir;
    fat32->fat32_delete_dir = fat32_delete_dir;
}

// Function to initialize the file system on a given drive
void init_fs(drive_t* drive) {
    if (drive->type == DRIVE_TYPE_ATA) {
        printf("Try to Init fs on ATA drive %s: %s with %u sectors\n", drive->name, drive->model, drive->sectors);
        printf("  ATA base: 0x%X, is_master: %d\n", drive->base, drive->is_master);
        
        // Allocate 512 bytes for boot sector (not 512 pointers!)
        uint8_t buffer[512];
        boot_sector_t* boot_sector = (boot_sector_t*)buffer;

        uint32_t partition_lba = 0;  // Track partition offset
        
        printf("Reading MBR/boot sector from LBA 0...\n");
        if (!ata_read_sector(drive->base, 0, buffer, drive->is_master)) {
            printf("Failed to read boot sector.\n");
            return;
        }
        
        // Check for MBR signature (0x55AA at offset 510)
        if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
            // Check if this is an MBR (has partition table) or just a boot sector
            // MBR partition entries start at offset 446
            uint8_t* partition_entry = &buffer[446];
            uint8_t status = partition_entry[0];
            uint8_t partition_type = partition_entry[4];
            
            // If we have a valid partition type, this is an MBR
            if (partition_type != 0x00) {
                printf("Detected MBR with partition table.\n");
                printf("  Partition 1 type: 0x%02X (0x0B/0x0C = FAT32)\n", partition_type);
                
                // Get LBA start of first partition (little-endian, 4 bytes at offset 8)
                partition_lba = *(uint32_t*)&partition_entry[8];
                printf("  Partition 1 starts at LBA %u\n", partition_lba);
                
                // Read the actual filesystem boot sector from the partition
                printf("Reading FAT32 boot sector from partition at LBA %u...\n", partition_lba);
                if (!ata_read_sector(drive->base, partition_lba, buffer, drive->is_master)) {
                    printf("Failed to read partition boot sector.\n");
                    return;
                }
                printf("Partition boot sector read successful.\n");
            }
        }
        
        printf("Boot sector read successful.\n");

        // Debug: Show first 32 bytes
        printf("First 32 bytes of boot sector:\n");
        for (int i = 0; i < 32; i++) {
            printf("%02X ", buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n");

        // Create a local, null-terminated copy of fsType
        char fs_type[9] = {0};  // 8 bytes + 1 for null terminator
        memcpy(fs_type, boot_sector->file_system_type, 8);
        fs_type[8] = '\0';  // Ensure null termination

        printf("Filesystem type (raw): '%s'\n", fs_type);
        
        // Trim trailing spaces from fs_type
        int len = 8;
        while (len > 0 && fs_type[len - 1] == ' ') {
            fs_type[len - 1] = '\0';
            len--;
        }
        
        printf("Filesystem type (trimmed): '%s'\n", fs_type);

        // Detect filesystem type by examining the boot sector
        // Use strncmp to compare only the significant characters
        if (strcmp(fs_type, "FAT12") == 0) {
            printf("Detected FAT12 filesystem on drive %s.\n", drive->name);
            //fat12_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT16") == 0) {
            printf("Detected FAT16 filesystem on drive %s.\n", drive->name);
            //fat16_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT32") == 0) {
            printf("Detected FAT32 filesystem on drive %s.\n", drive->name);

            // Initialize the fat32 class structure (only needed once)
            static bool fat32_class_initialized = false;
            if (!fat32_class_initialized) {
                printf("Initializing FAT32 class...\n");
                ctor_fat32_class(&fat32);
                fat32_class_initialized = true;
            }
            
            // Copy the already-read boot sector to fat32's global variable
            printf("Copying boot sector to FAT32 module (drive: %s)...\n", drive->name);
            extern struct fat32_boot_sector boot_sector;
            memcpy(&boot_sector, buffer, 512);
            
            // Set FAT32 globals directly (avoid re-reading boot sector)
            extern unsigned short ata_base_address;
            extern bool ata_is_master;
            extern unsigned int current_directory_cluster;
            extern unsigned int partition_lba_offset;
            
            printf("Setting FAT32 globals: base=0x%X, is_master=%d, rootCluster=%u, partitionOffset=%u\n",
                   drive->base, drive->is_master, boot_sector.root_cluster, partition_lba);
            
            ata_base_address = drive->base;
            ata_is_master = drive->is_master;
            current_directory_cluster = boot_sector.root_cluster;
            partition_lba_offset = partition_lba;  // Set the partition offset!
            
            printf("FAT32 initialized for drive %s\n", drive->name);
            
        } else if (memcmp(boot_sector->oem_name, "NTFS    ", 8) == 0) {
            printf("Detected NTFS filesystem on drive %s.\n", drive->name);
            //ntfs_init_fs(drive->base, drive->is_master);
        } else {
            printf("Unknown or unsupported filesystem.\n");
        }

        // free(boot_sector);
        // boot_sector = NULL;
    }

}

void print_raw_boot_sector(uint16_t* data, size_t length) {
    printf("Boot sector raw data:\n");
    for (size_t i = 0; i < length; i++) {
        printf("%04X ", data[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

/**
 * Auto-mount all detected drives
 * Called during system initialization to ensure filesystems are ready
 */
void auto_mount_all_drives(void) {
    extern drive_t detected_drives[];
    extern short drive_count;
    extern drive_t* current_drive;  // Shell needs this set
    
    if (drive_count == 0) {
        printf("Auto-mount: No drives detected\n");
        return;
    }
    
    printf("\n=== Auto-mounting detected drives ===\n");
    
    int mounted_count = 0;
    bool first_drive_set = false;
    
    // Mount all detected drives
    for (int i = 0; i < drive_count; i++) {
        drive_t* drive = &detected_drives[i];
        
        if (drive->type == DRIVE_TYPE_ATA) {
            printf("Mounting %s (%s)...\n", drive->name, drive->model);
            
            // Initialize filesystem on this drive
            init_fs(drive);
            
            // Set first mounted drive as current
            if (!first_drive_set) {
                current_drive = drive;
                first_drive_set = true;
                printf("  -> Set as default drive\n");
            }
            
            mounted_count++;
        }
        else if (drive->type == DRIVE_TYPE_FDD) {
            printf("Mounting %s (Floppy Drive %d)...\n", 
                   drive->name, drive->fdd_drive_no);
            
            // Initialize FAT12 filesystem
            if (fat12_init_fs(drive->fdd_drive_no)) {
                printf("  -> FAT12 filesystem ready\n");
                
                // Set first mounted drive as current if no HDD
                if (!first_drive_set) {
                    current_drive = drive;
                    first_drive_set = true;
                    printf("  -> Set as default drive\n");
                }
                
                mounted_count++;
            } else {
                printf("  -> Failed to mount\n");
            }
        }
    }
    
    printf("=== Auto-mount complete: %d/%d drives mounted ===\n\n", 
           mounted_count, drive_count);
    
    if (current_drive) {
        printf("Active drive: %s\n", current_drive->name);
    }
}
