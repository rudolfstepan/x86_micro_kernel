#ifndef FAT32_H
#define FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include "fat32_types.h"
#include "fat32_defines.h"
#include "lib/libc/definitions.h"
#include "drivers/bus/drives.h"  // For drive_t

// ============================================================================
// Public FAT32 API
// ============================================================================

// Initialization
int fat32_init_fs(unsigned short base, bool ata_is_master);

// Directory Operations
bool fat32_read_dir(const char *path);
bool fat32_create_dir(const char* dirname);
bool fat32_delete_dir(const char* dirname);
bool fat32_change_directory(const char *path);

// File Operations
FILE* fat32_open_file(const char* filename, const char* mode);
int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytesToRead);
bool fat32_create_file(const char* filename);
bool fat32_delete_file(const char* filename);
int fat32_load_file(const char* filename, void* loadAddress);

// Debug Functions
void ata_debug_bootsector(drive_t* drive);

// Utility Functions (also available publicly for class construction)
void formatFilename(char* dest, unsigned char* src);
void convert_to_83_format(unsigned char* dest, const char* src);
int compare_names(const char* fatName, const char* regularName);
void set_fat32_time(unsigned short* time, unsigned short* date);

// ============================================================================
// FAT32 Class Structure (Optional - for compatibility)
// ============================================================================
typedef struct {
    // Cluster and Sector Operations
    void (*read_cluster)(struct Fat32BootSector* bs, unsigned int cluster_number, void* buffer);
    unsigned int (*cluster_to_sector)(struct Fat32BootSector* bs, unsigned int cluster);
    unsigned int (*get_entries_per_cluster)(struct Fat32BootSector* bs);
    unsigned int (*get_total_clusters)(struct Fat32BootSector* bs);
    unsigned int (*get_first_data_sector)(struct Fat32BootSector* bs);

    // FAT Table Operations
    unsigned int (*read_fat_entry)(struct Fat32BootSector* bs, unsigned int cluster);
    bool (*write_fat_entry)(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
    bool (*mark_cluster_in_fat)(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
    bool (*link_cluster_to_chain)(struct Fat32BootSector* bs, unsigned int parentCluster, unsigned int newCluster);
    bool (*free_cluster_chain)(struct Fat32BootSector* boot_sector, unsigned int startCluster);
    unsigned int (*find_free_cluster)(struct Fat32BootSector* bs);
    unsigned int (*allocate_new_cluster)(struct Fat32BootSector* boot_sector);
    unsigned int (*get_next_cluster_in_chain)(struct Fat32BootSector* bs, unsigned int currentCluster);
    bool (*is_end_of_cluster_chain)(unsigned int cluster);

    // Directory and Entry Management
    void (*initialize_new_directory_entries)(struct FAT32DirEntry* entries, unsigned int newDirCluster, unsigned int parentCluster);
    void (*create_directory_entry)(struct FAT32DirEntry* entry, const char* name, unsigned int cluster, unsigned char attributes);
    bool (*add_entry_to_directory)(struct Fat32BootSector* bs, unsigned int parentCluster, const char* name, unsigned int newDirCluster, unsigned char attributes);
    bool (*remove_entry_from_directory)(struct Fat32BootSector* boot_sector, unsigned int parentCluster, struct FAT32DirEntry* entry);
    unsigned int (*find_next_cluster)(struct Fat32BootSector* bs, const char* dirName, unsigned int currentCluster);
    void (*read_cluster_dir_entries)(unsigned int currentCluster);
    bool (*write_cluster)(struct Fat32BootSector* bs, unsigned int cluster, const struct FAT32DirEntry* entries);
    unsigned int (*read_start_cluster)(struct FAT32DirEntry* entry);
    struct FAT32DirEntry* (*findFileInDirectory)(const char* filename);
    bool (*fat32_change_directory)(const char* path);

    // File and Data Management
    int (*fat32_load_file)(const char* filename, void* loadAddress);

    // Formatting and Utility Functions
    void (*formatFilename)(char* dest, unsigned char* src);
    void (*convert_to_83_format)(unsigned char* dest, const char* src);
    int (*compare_names)(const char* fatName, const char* regularName);
    void (*set_fat32_time)(unsigned short* time, unsigned short* date);

    // Public functions
    int (*fat32_init_fs)(unsigned short base, bool ata_is_master);

    // Directory operations
    bool (*fat32_read_dir)(const char* path);
    bool (*fat32_create_dir)(const char* dirname);
    bool (*fat32_delete_dir)(const char* dirname);

    // File operations
    FILE* (*fat32_open_file)(const char* filename, const char* mode);
    int (*fat32_read_file)(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytesToRead);
    bool (*fat32_create_file)(const char* filename);
    bool (*fat32_delete_file)(const char* filename);
} fat32_class_t;

// Global instance of the fat32 class (for backward compatibility)
extern fat32_class_t fat32;
void ctor_fat32_class(fat32_class_t* fat32);

#endif /* FAT32_H */
