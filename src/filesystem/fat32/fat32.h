#ifndef FAT32_H    /* This is an "include guard" */
#define FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "toolchain/string.h"
#include "toolchain/stdlib.h"
#include "toolchain/definitions.h"
#include "drivers/ata/ata.h"

#define SECTOR_SIZE 512
#define FIRST_CLUSTER_OF_FILE(clusterHigh, clusterLow) (((clusterHigh) << 16) | (clusterLow))
#define DIRECTORY_ENTRY_SIZE 32  // Size of a directory entry in FAT32
#define ATTR_DIRECTORY 0x10

#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_EOC_MAX 0x0FFFFFFF
#define INVALID_CLUSTER 0xFFFFFFFF
#define MAX_PATH_LENGTH 256


#pragma pack(push, 1)
struct FAT32DirEntry {
    uint8_t name[11];             // Short name (8.3 format)
    uint8_t attr;                 // File attributes
    uint8_t ntRes;                // Reserved for use by Windows NT
    uint8_t crtTimeTenth;         // Millisecond stamp at file creation time
    uint16_t crtTime;             // Time file was created
    uint16_t crtDate;             // Date file was created
    uint16_t lastAccessDate;      // Last access date
    uint16_t firstClusterHigh;    // High word of the first data cluster number
    uint16_t writeTime;             // Time of last write
    uint16_t writeDate;             // Date of last write
    uint16_t firstClusterLow;     // Low word of the first data cluster number
    uint32_t fileSize;            // File size in bytes
};
#pragma pack(pop)

#pragma pack(push, 1)
struct Fat32BootSector {
    uint8_t jumpBoot[3];               // Jump instruction to boot code
    uint8_t OEMName[8];                // OEM Name and version
    uint16_t bytesPerSector;           // Bytes per sector
    uint8_t sectorsPerCluster;         // Sectors per cluster
    uint16_t reservedSectorCount;      // Reserved sector count (including boot sector)
    uint8_t numberOfFATs;              // Number of FATs
    uint16_t rootEntryCount;           // (Not used in FAT32)
    uint16_t totalSectors16;           // Total sectors (16-bit), if 0, use totalSectors32
    uint8_t mediaType;                 // Media type
    uint16_t FATSize16;                // (Not used in FAT32)
    uint16_t sectorsPerTrack;          // Sectors per track (for media formatting)
    uint16_t numberOfHeads;            // Number of heads (for media formatting)
    uint32_t hiddenSectors;            // Hidden sectors (preceding the partition)
    uint32_t totalSectors32;           // Total sectors (32-bit)
    uint32_t FATSize32;                // Sectors per FAT
    uint16_t flags;                    // Flags
    uint16_t version;                  // FAT32 version
    uint32_t rootCluster;              // Root directory start cluster
    uint16_t FSInfo;                   // FSInfo sector
    uint16_t backupBootSector;         // Backup boot sector
    uint8_t reserved[12];              // Reserved bytes
    uint8_t driveNumber;               // Drive number
    uint8_t reserved1;                 // Reserved byte
    uint8_t bootSignature;             // Boot signature (indicates next three fields are present)
    uint32_t volumeID;                 // Volume ID serial number
    uint8_t volumeLabel[11];           // Volume label
    uint8_t fileSystemType[8];         // File system type label
};
#pragma pack(pop)

// external definitions which are defined in fat32.c but used in other files
extern struct Fat32BootSector boot_sector;
extern unsigned int current_directory_cluster; // Default root directory cluster for FAT32
extern unsigned short ata_base_address;
extern bool ata_is_master;



// a class to hold the fat32 filesystem functions
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

// a global instance of the fat32 class
extern fat32_class_t fat32;
void ctor_fat32_class(fat32_class_t* fat32);


// Cluster and Sector Operations
void read_cluster(struct Fat32BootSector* bs, unsigned int cluster_number, void* buffer);
unsigned int cluster_to_sector(struct Fat32BootSector* bs, unsigned int cluster);
unsigned int get_entries_per_cluster(struct Fat32BootSector* bs);
unsigned int get_total_clusters(struct Fat32BootSector* bs);
unsigned int get_first_data_sector(struct Fat32BootSector* bs);

// FAT Table Operations
unsigned int read_fat_entry(struct Fat32BootSector* bs, unsigned int cluster);
bool write_fat_entry(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool mark_cluster_in_fat(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool link_cluster_to_chain(struct Fat32BootSector* bs, unsigned int parentCluster, unsigned int newCluster);
bool free_cluster_chain(struct Fat32BootSector* boot_sector, unsigned int startCluster);
unsigned int find_free_cluster(struct Fat32BootSector* bs);
unsigned int allocate_new_cluster(struct Fat32BootSector* boot_sector);
unsigned int get_next_cluster_in_chain(struct Fat32BootSector* bs, unsigned int currentCluster);
bool is_end_of_cluster_chain(unsigned int cluster);

// Directory and Entry Management
void initialize_new_directory_entries(struct FAT32DirEntry* entries, unsigned int newDirCluster, unsigned int parentCluster);
void create_directory_entry(struct FAT32DirEntry* entry, const char* name, unsigned int cluster, unsigned char attributes);
bool add_entry_to_directory(struct Fat32BootSector* bs, unsigned int parentCluster, const char* name, unsigned int newDirCluster, unsigned char attributes);
bool remove_entry_from_directory(struct Fat32BootSector* boot_sector, unsigned int parentCluster, struct FAT32DirEntry* entry);
unsigned int find_next_cluster(struct Fat32BootSector* bs, const char *dirName, unsigned int currentCluster);
void read_cluster_dir_entries(unsigned int currentCluster);
bool write_cluster(struct Fat32BootSector* bs, unsigned int cluster, const struct FAT32DirEntry* entries);
unsigned int read_start_cluster(struct FAT32DirEntry* entry);
struct FAT32DirEntry* findFileInDirectory(const char* filename);
bool fat32_change_directory(const char *path);

// File and Data Management
int fat32_load_file(const char* filename, void* loadAddress);

// Formatting and Utility Functions
void formatFilename(char* dest, unsigned char* src);
void convert_to_83_format(unsigned char* dest, const char* src);
int compare_names(const char* fatName, const char* regularName);
void set_fat32_time(unsigned short* time, unsigned short* date);

// public functions
int fat32_init_fs(unsigned short base, bool ata_is_master);

// directory operations
bool fat32_read_dir(const char *path);
bool fat32_create_dir(const char* dirname);
bool fat32_delete_dir(const char* dirname);

// file operations
FILE* fat32_open_file(const char* filename, const char* mode);
int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size, unsigned int bytesToRead);
bool fat32_create_file(const char* filename);
bool fat32_delete_file(const char* filename);

void ata_debug_bootsector(drive_t* drive);




#endif