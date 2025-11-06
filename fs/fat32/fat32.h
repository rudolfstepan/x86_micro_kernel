#ifndef FAT32_H    /* This is an "include guard" */
#define FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/libc/string.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/definitions.h"
#include "drivers/block/ata.h"

#define SECTOR_SIZE 512
#define FIRST_CLUSTER_OF_FILE(clusterHigh, clusterLow) (((clusterHigh) << 16) | (clusterLow))
#define DIRECTORY_ENTRY_SIZE 32  // Size of a directory entry in FAT32
#define ATTR_DIRECTORY 0x10

#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_EOC_MAX 0x0FFFFFFF
#define INVALID_CLUSTER 0xFFFFFFFF
#define MAX_PATH_LENGTH 256


#pragma pack(push, 1)
struct fat32_dir_entry {
    uint8_t name[11];             // Short name (8.3 format)
    uint8_t attr;                 // File attributes
    uint8_t nt_res;               // Reserved for use by Windows NT
    uint8_t crt_time_tenth;       // Millisecond stamp at file creation time
    uint16_t crt_time;            // Time file was created
    uint16_t crt_date;            // Date file was created
    uint16_t last_access_date;    // Last access date
    uint16_t first_cluster_high;  // High word of the first data cluster number
    uint16_t write_time;          // Time of last write
    uint16_t write_date;          // Date of last write
    uint16_t first_cluster_low;   // Low word of the first data cluster number
    uint32_t file_size;           // File size in bytes
};
#pragma pack(pop)

#pragma pack(push, 1)
struct fat32_boot_sector {
    uint8_t jump_boot[3];              // Jump instruction to boot code
    uint8_t oem_name[8];               // OEM Name and version
    uint16_t bytes_per_sector;         // Bytes per sector
    uint8_t sectors_per_cluster;       // Sectors per cluster
    uint16_t reserved_sector_count;    // Reserved sector count (including boot sector)
    uint8_t number_of_fats;            // Number of FATs
    uint16_t root_entry_count;         // (Not used in FAT32)
    uint16_t total_sectors_16;         // Total sectors (16-bit), if 0, use total_sectors_32
    uint8_t media_type;                // Media type
    uint16_t fat_size_16;              // (Not used in FAT32)
    uint16_t sectors_per_track;        // Sectors per track (for media formatting)
    uint16_t number_of_heads;          // Number of heads (for media formatting)
    uint32_t hidden_sectors;           // Hidden sectors (preceding the partition)
    uint32_t total_sectors_32;         // Total sectors (32-bit)
    uint32_t fat_size_32;              // Sectors per FAT
    uint16_t flags;                    // Flags
    uint16_t version;                  // FAT32 version
    uint32_t root_cluster;             // Root directory start cluster
    uint16_t fs_info;                  // FSInfo sector
    uint16_t backup_boot_sector;       // Backup boot sector
    uint8_t reserved[12];              // Reserved bytes
    uint8_t drive_number;              // Drive number
    uint8_t reserved1;                 // Reserved byte
    uint8_t boot_signature;            // Boot signature (indicates next three fields are present)
    uint32_t volume_id;                // Volume ID serial number
    uint8_t volume_label[11];          // Volume label
    uint8_t file_system_type[8];       // File system type label
    uint8_t boot_code[420];            // Boot code (padded to make structure exactly 512 bytes)
    uint16_t boot_sector_signature;    // 0xAA55 at offset 510-511
};
#pragma pack(pop)

// external definitions which are defined in fat32.c but used in other files
extern struct fat32_boot_sector boot_sector;
extern unsigned int current_directory_cluster; // Default root directory cluster for FAT32
extern unsigned short ata_base_address;
extern bool ata_is_master;



// a class to hold the fat32 filesystem functions
typedef struct {
    // Cluster and Sector Operations
    void (*read_cluster)(struct fat32_boot_sector* bs, unsigned int cluster_number, void* buffer);
    unsigned int (*cluster_to_sector)(struct fat32_boot_sector* bs, unsigned int cluster);
    unsigned int (*get_entries_per_cluster)(struct fat32_boot_sector* bs);
    unsigned int (*get_total_clusters)(struct fat32_boot_sector* bs);
    unsigned int (*get_first_data_sector)(struct fat32_boot_sector* bs);

    // FAT Table Operations
    unsigned int (*read_fat_entry)(struct fat32_boot_sector* bs, unsigned int cluster);
    bool (*write_fat_entry)(struct fat32_boot_sector* bs, unsigned int cluster, unsigned int value);
    bool (*mark_cluster_in_fat)(struct fat32_boot_sector* bs, unsigned int cluster, unsigned int value);
    bool (*link_cluster_to_chain)(struct fat32_boot_sector* bs, unsigned int parent_cluster, unsigned int new_cluster);
    bool (*free_cluster_chain)(struct fat32_boot_sector* boot_sector, unsigned int start_cluster);
    unsigned int (*find_free_cluster)(struct fat32_boot_sector* bs);
    unsigned int (*allocate_new_cluster)(struct fat32_boot_sector* boot_sector);
    unsigned int (*get_next_cluster_in_chain)(struct fat32_boot_sector* bs, unsigned int current_cluster);
    bool (*is_end_of_cluster_chain)(unsigned int cluster);

    // Directory and Entry Management
    void (*initialize_new_directory_entries)(struct fat32_dir_entry* entries, unsigned int new_dir_cluster, unsigned int parent_cluster);
    void (*create_directory_entry)(struct fat32_dir_entry* entry, const char* name, unsigned int cluster, unsigned char attributes);
    bool (*add_entry_to_directory)(struct fat32_boot_sector* bs, unsigned int parent_cluster, const char* name, unsigned int new_dir_cluster, unsigned char attributes);
    bool (*remove_entry_from_directory)(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, struct fat32_dir_entry* entry);
    unsigned int (*find_next_cluster)(struct fat32_boot_sector* bs, const char* dir_name, unsigned int current_cluster);
    void (*read_cluster_dir_entries)(unsigned int current_cluster);
    bool (*write_cluster)(struct fat32_boot_sector* bs, unsigned int cluster, const struct fat32_dir_entry* entries);
    unsigned int (*read_start_cluster)(struct fat32_dir_entry* entry);
    struct fat32_dir_entry* (*find_file_in_directory)(const char* filename);
    bool (*fat32_change_directory)(const char* path);

    // File and Data Management
    int (*fat32_load_file)(const char* filename, void* load_address);

    // Formatting and Utility Functions
    void (*format_filename)(char* dest, unsigned char* src);
    void (*convert_to_83_format)(unsigned char* dest, const char* src);
    int (*compare_names)(const char* fat_name, const char* regular_name);
    void (*set_fat32_time)(unsigned short* time, unsigned short* date);

    // Public functions
    int (*fat32_init_fs)(unsigned short base, bool ata_is_master);

    // Directory operations
    bool (*fat32_read_dir)(const char* path);
    bool (*fat32_create_dir)(const char* dirname);
    bool (*fat32_delete_dir)(const char* dirname);

    // File operations
    FILE* (*fat32_open_file)(const char* filename, const char* mode);
    int (*fat32_read_file)(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read);
    bool (*fat32_create_file)(const char* filename);
    bool (*fat32_delete_file)(const char* filename);
} fat32_class_t;

// a global instance of the fat32 class
extern fat32_class_t fat32;
void ctor_fat32_class(fat32_class_t* fat32);


// Cluster and Sector Operations
void read_cluster(struct fat32_boot_sector* bs, unsigned int cluster_number, void* buffer);
unsigned int cluster_to_sector(struct fat32_boot_sector* bs, unsigned int cluster);
unsigned int get_entries_per_cluster(struct fat32_boot_sector* bs);
unsigned int get_total_clusters(struct fat32_boot_sector* bs);
unsigned int get_first_data_sector(struct fat32_boot_sector* bs);

// FAT Table Operations
unsigned int read_fat_entry(struct fat32_boot_sector* bs, unsigned int cluster);
bool write_fat_entry(struct fat32_boot_sector* bs, unsigned int cluster, unsigned int value);
bool mark_cluster_in_fat(struct fat32_boot_sector* bs, unsigned int cluster, unsigned int value);
bool link_cluster_to_chain(struct fat32_boot_sector* bs, unsigned int parent_cluster, unsigned int new_cluster);
bool free_cluster_chain(struct fat32_boot_sector* boot_sector, unsigned int start_cluster);
unsigned int find_free_cluster(struct fat32_boot_sector* bs);
unsigned int allocate_new_cluster(struct fat32_boot_sector* boot_sector);
unsigned int get_next_cluster_in_chain(struct fat32_boot_sector* bs, unsigned int current_cluster);
bool is_end_of_cluster_chain(unsigned int cluster);

// Directory and Entry Management
void initialize_new_directory_entries(struct fat32_dir_entry* entries, unsigned int new_dir_cluster, unsigned int parent_cluster);
void create_directory_entry(struct fat32_dir_entry* entry, const char* name, unsigned int cluster, unsigned char attributes);
bool add_entry_to_directory(struct fat32_boot_sector* bs, unsigned int parent_cluster, const char* name, unsigned int new_dir_cluster, unsigned char attributes);
bool remove_entry_from_directory(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, struct fat32_dir_entry* entry);
unsigned int find_next_cluster(struct fat32_boot_sector* bs, const char *dir_name, unsigned int current_cluster);
void read_cluster_dir_entries(unsigned int current_cluster);
bool write_cluster(struct fat32_boot_sector* bs, unsigned int cluster, const struct fat32_dir_entry* entries);
unsigned int read_start_cluster(struct fat32_dir_entry* entry);
struct fat32_dir_entry* find_file_in_directory(const char* filename);
bool fat32_change_directory(const char *path);

// File and Data Management
int fat32_load_file(const char* filename, void* load_address);

// Formatting and Utility Functions
void format_filename(char* dest, unsigned char* src);
void convert_to_83_format(unsigned char* dest, const char* src);
int compare_names(const char* fat_name, const char* regular_name);
void set_fat32_time(unsigned short* time, unsigned short* date);

// public functions
int fat32_init_fs(unsigned short base, bool ata_is_master);

// directory operations
bool fat32_read_dir(const char *path);
bool fat32_create_dir(const char* dirname);
bool fat32_delete_dir(const char* dirname);

// file operations
FILE* fat32_open_file(const char* filename, const char* mode);
int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read);
bool fat32_create_file(const char* filename);
bool fat32_delete_file(const char* filename);

void ata_debug_bootsector(drive_t* drive);




#endif