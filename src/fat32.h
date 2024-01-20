#ifndef FAT32_H    /* This is an "include guard" */
#define FAT32_H

#include <stdbool.h>


#define FIRST_CLUSTER_OF_FILE(clusterHigh, clusterLow) (((clusterHigh) << 16) | (clusterLow))

#define DIRECTORY_ENTRY_SIZE 32  // Size of a directory entry in FAT32
#define ATTR_DIRECTORY 0x10

#define SUCCESS 0
#define FAILURE -1

#define FAT32_EOC_MIN 0x0FFFFFF8
#define FAT32_EOC_MAX 0x0FFFFFFF
#define INVALID_CLUSTER 0xFFFFFFFF
#define MAX_PATH_LENGTH 256


#pragma pack(push, 1)
struct FAT32DirEntry {
    unsigned char name[11];        // Short name (8.3 format)
    unsigned char attr;            // File attributes
    unsigned char ntRes;           // Reserved for use by Windows NT
    unsigned char crtTimeTenth;    // Millisecond stamp at file creation time
    unsigned short crtTime;        // Time file was created
    unsigned short crtDate;        // Date file was created
    unsigned short lastAccessDate; // Last access date
    unsigned short firstClusterHigh; // High word of the first data cluster number
    unsigned short wrtTime;        // Time of last write
    unsigned short wrtDate;        // Date of last write
    unsigned short firstClusterLow; // Low word of the first data cluster number
    unsigned int fileSize;         // File size in bytes
};
#pragma pack(pop)


#pragma pack(push, 1)
struct Fat32BootSector {
    unsigned char jumpBoot[3];        // Jump instruction to boot code
    unsigned char OEMName[8];         // OEM Name and version
    unsigned short bytesPerSector;    // Bytes per sector
    unsigned char sectorsPerCluster;  // Sectors per cluster
    unsigned short reservedSectorCount; // Reserved sector count (including boot sector)
    unsigned char numberOfFATs;       // Number of FATs
    unsigned short rootEntryCount;    // (Not used in FAT32)
    unsigned short totalSectors16;    // Total sectors (16-bit), if 0, use totalSectors32
    unsigned char mediaType;          // Media type
    unsigned short FATSize16;         // (Not used in FAT32)
    unsigned short sectorsPerTrack;   // Sectors per track (for media formatting)
    unsigned short numberOfHeads;     // Number of heads (for media formatting)
    unsigned int hiddenSectors;       // Hidden sectors (preceding the partition)
    unsigned int totalSectors32;      // Total sectors (32-bit)
    unsigned int FATSize32;           // Sectors per FAT
    unsigned short flags;             // Flags
    unsigned short version;           // FAT32 version
    unsigned int rootCluster;         // Root directory start cluster
    unsigned short FSInfo;            // FSInfo sector
    unsigned short backupBootSector;  // Backup boot sector
    unsigned char reserved[12];       // Reserved bytes
    unsigned char driveNumber;        // Drive number
    unsigned char reserved1;          // Reserved byte
    unsigned char bootSignature;      // Boot signature (indicates next three fields are present)
    unsigned int volumeID;            // Volume ID serial number
    unsigned char volumeLabel[11];    // Volume label
    unsigned char fileSystemType[8];  // File system type label
};
#pragma pack(pop)

extern struct Fat32BootSector boot_sector;

// private functions
void read_cluster(struct Fat32BootSector* bs, unsigned int cluster_number, void* buffer);
unsigned int clusterStartSector(struct Fat32BootSector* bs, unsigned int clusterNumber);
unsigned int cluster_to_sector(struct Fat32BootSector* bs, unsigned int cluster);
unsigned int readStartCluster(struct FAT32DirEntry* entry);
void readFileData(unsigned int startCluster, char* buffer, unsigned int size);
void openAndLoadFile(const char* filename);
int openAndLoadFileToBuffer(const char* filename, void* loadAddress);
int readFileDataToAddress(unsigned int startCluster, void* loadAddress, unsigned int fileSize);
void formatFilename(char* dest, unsigned char* src);
int isEndOfClusterChain(unsigned int cluster);
struct FAT32DirEntry* findFileInDirectory(const char* filename);
bool try_directory_path(const char *path);

unsigned int find_next_cluster(struct Fat32BootSector* bs, const char *dirName, unsigned int currentCluster);
unsigned int get_next_cluster_in_chain(struct Fat32BootSector* bs, unsigned int currentCluster);

void initialize_new_directory_entries(struct FAT32DirEntry* entries, unsigned int newDirCluster, unsigned int parentCluster);
void create_directory_entry(struct FAT32DirEntry* entry, const char* name, unsigned int cluster, unsigned char attributes);
bool add_entry_to_directory(struct Fat32BootSector* bs, unsigned int parentCluster, const char* name, unsigned int newDirCluster, unsigned char attributes);
unsigned int find_free_cluster(struct Fat32BootSector* bs);
bool mark_cluster_in_fat(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool write_cluster(struct Fat32BootSector* bs, unsigned int cluster, const struct FAT32DirEntry* entries);
unsigned int read_fat_entry(struct Fat32BootSector* bs, unsigned int cluster);
bool write_fat_entry(struct Fat32BootSector* bs, unsigned int cluster, unsigned int value);
bool link_cluster_to_chain(struct Fat32BootSector* bs, unsigned int parentCluster, unsigned int newCluster);

unsigned int get_entries_per_cluster(struct Fat32BootSector* bs);
unsigned int get_total_clusters(struct Fat32BootSector* bs);
unsigned int get_first_data_sector(struct Fat32BootSector* bs);

void convert_to_83_format(unsigned char* dest, const char* src);
void set_fat32_time(unsigned short* time, unsigned short* date);

// public functions
int init_fs(void);
void read_directory();

bool read_directory_path(const char *path);
int read_directory_to_buffer(const char *path, char *buffer, unsigned int *size);

bool change_directory(const char* path);
bool create_directory(const char* dirname);
bool create_file(const char* filename);
bool delete_file(const char* filename);
bool delete_directory(const char* dirname);

#endif