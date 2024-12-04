#include "filesystem.h"
#include "drivers/ata/ata.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"
#include "toolchain/stdio.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/io/io.h"
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
    fat32->findFileInDirectory = findFileInDirectory;
    fat32->fat32_change_directory = fat32_change_directory;

    // File and Data Management
    fat32->fat32_load_file = fat32_load_file;

    // Formatting and Utility Functions
    fat32->formatFilename = formatFilename;
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
        // Initialize file system for ATA drive
        // Read the boot sector
        // boot_sector_t* boot_sector = (boot_sector_t*)malloc(sizeof(boot_sector_t));
        // if (boot_sector == NULL) {
        //     // Handle allocation failure (e.g., print an error and exit)
        //     printf("BootSector Memory allocation failed.\n");
        //     return;
        // }

        void* buffer[512];
        boot_sector_t* boot_sector = (boot_sector_t*)buffer;

        if (!ata_read_sector(drive->base, 0, &buffer, drive->is_master)) {
            printf("Failed to read boot sector.\n");
            return;
        }

        //hex_dump(&boot_sector, 512);

        // Create a local, null-terminated copy of fsType
        char fs_type[9] = {0};  // 8 bytes + 1 for null terminator
        memcpy(fs_type, boot_sector->file_system_type, 8);
        fs_type[8] = '\0';  // Ensure null termination

        //printf("Filesystem type: %s\n", fs_type);

        // Detect filesystem type by examining the boot sector
        if (strcmp(fs_type, "FAT12   ") == 0) {
            printf("Detected FAT12 filesystem on drive %s.\n", drive->name);
            //fat12_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT16   ") == 0) {
            printf("Detected FAT16 filesystemo n drive %s.\n", drive->name);
            //fat16_init_fs(drive->base, drive->is_master);
        } else if (strcmp(fs_type, "FAT32   ") == 0) {
            printf("Detected FAT32 filesystem on drive %s.\n", drive->name);
            //fat32_init_fs(drive->base, drive->is_master);

            // initialize the fat32 filesystem
            fat32.fat32_init_fs(drive->base, drive->is_master);
            
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
