#ifndef FAT32_DEFINES_H
#define FAT32_DEFINES_H

// Sector and Cluster Constants
#define SECTOR_SIZE 512
#define DIRECTORY_ENTRY_SIZE 32

// FAT32 Cluster Values
#define FAT32_EOC_MIN 0x0FFFFFF8        // Minimum End-of-Chain marker
#define FAT32_EOC_MAX 0x0FFFFFFF        // Maximum End-of-Chain marker
#define INVALID_CLUSTER 0xFFFFFFFF       // Invalid cluster marker

// File Attributes
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// Path Constants
#define MAX_PATH_LENGTH 256

// Utility Macros
#define FIRST_CLUSTER_OF_FILE(clusterHigh, clusterLow) \
    (((clusterHigh) << 16) | (clusterLow))

#endif /* FAT32_DEFINES_H */
