#ifndef FAT32_INTERNAL_H
#define FAT32_INTERNAL_H

#include <stdbool.h>
#include "fat32_types.h"

// ============================================================================
// Internal Global State (defined in fat32_context.c)
// ============================================================================
extern struct Fat32BootSector boot_sector;
extern unsigned int current_directory_cluster;
extern unsigned short ata_base_address;
extern bool ata_is_master;

// ============================================================================
// Internal Cluster Operations
// ============================================================================
void read_cluster(struct Fat32BootSector* bs, unsigned int cluster_number, void* buffer);
unsigned int cluster_to_sector(struct Fat32BootSector* bs, unsigned int cluster);
unsigned int get_entries_per_cluster(struct Fat32BootSector* bs);
unsigned int get_total_clusters(struct Fat32BootSector* bs);
unsigned int get_first_data_sector(struct Fat32BootSector* bs);

// ============================================================================
// Internal FAT Table Operations
// ============================================================================
unsigned int read_fat_entry(struct Fat32BootSector* bs, unsigned int cluster);
bool write_fat_entry(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool mark_cluster_in_fat(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool link_cluster_to_chain(struct Fat32BootSector* bs, unsigned int parentCluster, unsigned int newCluster);
bool free_cluster_chain(struct Fat32BootSector* boot_sector, unsigned int startCluster);
unsigned int find_free_cluster(struct Fat32BootSector* bs);
unsigned int allocate_new_cluster(struct Fat32BootSector* boot_sector);
unsigned int get_next_cluster_in_chain(struct Fat32BootSector* bs, unsigned int currentCluster);
bool is_end_of_cluster_chain(unsigned int cluster);

// ============================================================================
// Internal Directory and Entry Management
// ============================================================================
void initialize_new_directory_entries(struct FAT32DirEntry* entries, unsigned int newDirCluster, unsigned int parentCluster);
void create_directory_entry(struct FAT32DirEntry* entry, const char* name, unsigned int cluster, unsigned char attributes);
bool add_entry_to_directory(struct Fat32BootSector* bs, unsigned int parentCluster, const char* name, unsigned int newDirCluster, unsigned char attributes);
bool remove_entry_from_directory(struct Fat32BootSector* boot_sector, unsigned int parentCluster, struct FAT32DirEntry* entry);
unsigned int find_next_cluster(struct Fat32BootSector* bs, const char *dirName, unsigned int currentCluster);
void read_cluster_dir_entries(unsigned int currentCluster);
bool write_cluster(struct Fat32BootSector* bs, unsigned int cluster, const struct FAT32DirEntry* entries);
unsigned int read_start_cluster(struct FAT32DirEntry* entry);
struct FAT32DirEntry* findFileInDirectory(const char* filename);

// ============================================================================
// Internal Formatting and Utility Functions
// ============================================================================
void formatFilename(char* dest, unsigned char* src);
void convert_to_83_format(unsigned char* dest, const char* src);
int compare_names(const char* fatName, const char* regularName);
void set_fat32_time(unsigned short* time, unsigned short* date);

#endif /* FAT32_INTERNAL_H */
