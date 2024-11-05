/*
 * FAT12 Disk Reading and Directory Population Process
 *
 * This code implements the process of reading a FAT12 file system from a floppy disk,
 * specifically for a standard 1.44MB disk, and populating the FAT12 structure with 
 * boot sector, FAT table, and root directory information.
 *
 * =============================================================
 * Disk Geometry and Logical to Physical Sector Conversion
 * =============================================================
 * In a 1.44MB floppy disk, the layout typically includes:
 *   - 80 tracks (or cylinders)
 *   - 2 heads (sides), for double-sided disks
 *   - 18 sectors per track
 *
 * To access a specific sector on the disk, we calculate the track, head, and sector
 * values from a logical sector number. This ensures that we read the correct physical
 * location on the disk. The steps are as follows:
 *
 *   1. Calculate Track (Cylinder):
 *      - Formula: track = logical_sector / (sectors_per_track * heads)
 *      - This determines which cylinder (track) the data resides in.
 *
 *   2. Calculate Head:
 *      - Formula: head = (logical_sector / sectors_per_track) % heads
 *      - Dividing by sectors per track gives the position within the current track,
 *        and taking the remainder with the number of heads gives the correct side
 *        of the disk (0 or 1 for double-sided disks).
 *
 *   3. Calculate Sector:
 *      - Formula: sector = (logical_sector % sectors_per_track) + 1
 *      - The sector number within a track is typically 1-based, so after getting the
 *        remainder, we add 1 to match the hardware convention.
 *
 * These values are then passed to `fdc_read_sector` to read a specific sector from 
 * the disk, where:
 *    fdc_read_sector(drive, head, track, sector, buffer)
 *
 * =============================================================
 * FAT12 Initialization Process
 * =============================================================
 * The FAT12 initialization involves the following key steps:
 * 
 *   1. **Read the Boot Sector**:
 *      - The boot sector (first sector on the disk) contains critical information,
 *        such as bytes per sector, sectors per cluster, reserved sectors, FAT count,
 *        root directory entry count, and sectors per FAT.
 *      - This information is stored in the `fat12.bootSector` structure and is used
 *        for calculating the positions of the FAT, root directory, and data regions.
 *
 *   2. **Calculate Key Offsets**:
 *      - Using information from the boot sector, we calculate:
 *          - `fatStart`: Start of the FAT area, which begins after the reserved sectors.
 *          - `rootDirStart`: Start of the root directory, which comes after the FAT area.
 *          - `dataStart`: Start of the data area, which follows the root directory.
 *
 *   3. **Read the FAT Table**:
 *      - The FAT table is located after the reserved sectors. This table maps cluster
 *        chains for files and directories, indicating whether a cluster is free,
 *        in use, or the end of a file.
 *      - We read this table to populate the FAT structure, allowing the file system
 *        to locate files across the clusters.
 *
 *   4. **Read the Root Directory**:
 *      - The root directory is stored in a fixed location immediately after the FAT
 *        table. It consists of a predefined number of 32-byte entries that provide
 *        details for each file or directory, such as name, extension, attributes,
 *        starting cluster, and file size.
 *      - Each entry is processed as follows:
 *          - Entries starting with 0x00 are unused, marking the end of directory entries.
 *          - Entries starting with 0xE5 are deleted entries and are ignored.
 *          - Valid entries are processed, with attributes indicating if they are a
 *            file or a directory.
 *
 * =============================================================
 * Directory Entry Processing
 * =============================================================
 * Each entry in the root directory is processed and displayed. The process for each
 * entry includes:
 * 
 *   - **End of Directory Check**: If the first byte of the filename is 0x00, it 
 *     indicates that no further entries are present. The process stops at this point.
 * 
 *   - **Deleted Entry Check**: If the first byte of the filename is 0xE5, the entry
 *     is marked as deleted and skipped in the processing loop.
 * 
 *   - **Display Information**: For each active entry, the filename, extension,
 *     and attributes are printed, showing whether it is a file or directory.
 *
 * =============================================================
 * Example Directory Output
 * =============================================================
 * After successfully reading the root directory and skipping deleted or unused entries,
 * the program will display each valid entry in the format:
 * 
 *     File: Entry 21: INFO    .TXT
 *     Directory: Entry 12: EXAMPLES   <DIR>
 * 
 * This output provides a basic file listing, similar to a directory listing in MS-DOS.
 *
 */

#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include "drivers/keyboard/keyboard.h"

#define ROOT_ENTRY_SIZE 32
#define SECTOR_SIZE 512
#define ROOT_DIR_SECTORS 14
#define ROOT_DIR_BUFFER_SIZE (SECTOR_SIZE * ROOT_DIR_SECTORS)

// Global structures and buffers
FAT12 fat12;                                 // Global instance of FAT12 structure
DirectoryEntry entries[224];                 // Maximum root directory entries for FAT12 (224)
uint8_t root_buffer[ROOT_DIR_BUFFER_SIZE];   // Buffer to hold the root directory data
uint8_t boot_sector_buffer[SECTOR_SIZE];     // Buffer to hold the boot sector

int read_fat12(uint8_t drive, FAT12* fat12) {
    // Step 1: Read the boot sector (First sector of the disk)
    if (!fdc_read_sector(drive, 0, 0, 1, boot_sector_buffer)) {
        printf("Error reading boot sector.\n");
        return false;
    }

    // Copy boot sector data into FAT12 structure
    memcpy(&fat12->bootSector, boot_sector_buffer, sizeof(Fat12BootSector));

    // Verify the boot sector signature
    if (fat12->bootSector.bootSectorSignature != 0xAA55) {
        printf("Invalid boot sector signature.\n");
        return false;
    }

    // Calculate the start of the FAT, root directory, and data regions
    fat12->fatStart = fat12->bootSector.reservedSectors;  // FAT starts after reserved sectors
    fat12->rootDirStart = fat12->fatStart + (fat12->bootSector.fatCount * fat12->bootSector.sectorsPerFAT);  // Root dir starts after FAT area
    fat12->dataStart = fat12->rootDirStart + ((fat12->bootSector.rootEntryCount * ROOT_ENTRY_SIZE) / SECTOR_SIZE);  // Data area starts after root directory

    printf("FAT12 initialized:\n");
    printf("FAT Start Sector: %d\n", fat12->fatStart);
    printf("Root Directory Start Sector: %d\n", fat12->rootDirStart);
    printf("Data Region Start Sector: %d\n", fat12->dataStart);

    return true;  // Successfully initialized FAT12 structure
}

// Function to read the Root Directory into a static global buffer and then parse it
bool read_root_directory(int drive, DirectoryEntry* root_directory) {
    // Constants for a standard 1.44MB floppy disk
    const int sectors_per_track = 18;
    const int heads = 2;

    // Read each sector of the Root Directory into the global buffer `root_buffer`
    for (int i = 0; i < ROOT_DIR_SECTORS; i++) {
        int logical_sector = fat12.rootDirStart + i;

        // Calculate track, head, and sector from logical sector number
        int track = logical_sector / (sectors_per_track * heads);
        int head = (logical_sector / sectors_per_track) % heads;
        int sector = (logical_sector % sectors_per_track) + 1;  // Sectors are typically 1-based

        printf("Reading Root Directory sector %d (Track: %d, Head: %d, Sector: %d)\n", i, track, head, sector);

        if (!fdc_read_sector(drive, head, track, sector, root_buffer + (i * SECTOR_SIZE))) {
            printf("Error reading Root Directory sector %d\n", i);
            return false;
        }
    }

    // Copy the data from buffer into the root_directory structure array
    memcpy(root_directory, root_buffer, ROOT_ENTRY_SIZE * fat12.bootSector.rootEntryCount);

    return true;
}

// Function to initialize the FAT12 file system
bool fat12_init_fs() {
    if (read_fat12(0, &fat12)) {
        printf("FAT12 filesystem read successfully.\n");

        int num_entries = read_root_directory(0, entries);

        // Update to read the maximum expected number of entries
        int max_entries = fat12.bootSector.rootEntryCount;
        printf("Number of entries to read: %d\n", max_entries);

        if (num_entries > 0) {
            printf("Root directory read successfully.\n");

            for (int i = 0; i < max_entries; i++) {
                DirectoryEntry* entry = &entries[i];

                // Check for end of directory (unused entry)
                if ((unsigned char)entry->filename[0] == 0x00) {
                    printf("End of directory entries.\n");
                    break;  // Stop only when an unused entry is found
                }

                // Skip deleted entries
                if ((unsigned char)entry->filename[0] == 0xE5) {
                    continue;  // Ignore deleted entries
                }

                // Determine if entry is a directory or file
                if (entry->attributes & 0x10) {
                    printf("Directory: ");
                } else {
                    printf("File: ");
                }

                // Format filename and extension with null-terminators
                char filename[9] = {0};  // 8 chars + null terminator
                char extension[4] = {0}; // 3 chars + null terminator
                strncpy(filename, (const char*)entry->filename, 8);
                strncpy(extension, (const char*)entry->extension, 3);

                printf("Entry %d: %s.%s\n", i, filename, extension);

                if(i == 20){
                    // wait for user press enter
                    wait_for_enter();
                }
            }
        } else {
            printf("Error reading root directory.\n");
        }

    } else {
        printf("Failed to read FAT12 filesystem.\n");
        return false;
    }
    return true;
}

bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
    return false;
}

// // Function to display the Root Directory
// void display_root_directory(DirectoryEntry* root_directory, int num_entries) {
//     printf("Inhaltsverzeichnis der Diskette:\n");
//     printf("Dateiname    | Erweiterung | Groesse (Bytes)\n");
//     printf("--------------------------------------------\n");

//     for (int i = 0; i < num_entries; i++) {
//         DirectoryEntry* entry = &root_directory[i];

//         // Stop if the entry is unused
//         if ((unsigned char)entry->filename[0] == 0x00) {
//             break;  // No more entries
//         }

//         // Skip deleted entries
//         if ((unsigned char)entry->filename[0] == 0xE5) {
//             continue;
//         }

//         // Format filename and extension with null-terminators
//         char filename[9] = {0};  // 8 chars + null terminator
//         char extension[4] = {0}; // 3 chars + null terminator
//         strncpy(filename, (const char*)entry->filename, 8);
//         strncpy(extension, (const char*)entry->extension, 3);

//         // Ensure the filename and extension do not contain non-printable characters
//         for (int j = 0; j < 8; j++) if (filename[j] < 32) filename[j] = '\0';
//         for (int j = 0; j < 3; j++) if (extension[j] < 32) extension[j] = '\0';

//         // Display file or directory information
//         if (is_directory(entry)) {
//             printf("%-11s <DIR>            \n", filename);
//         } else if (is_file(entry)) {
//             printf("%-8s.%-3s    %10u\n", filename, extension, entry->fileSize);
//         }
//     }
// }

// // Function to read the contents of a directory in the FAT12 file system
// bool fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
//     printf("Reading FAT12 directory: %s\n", path);
//     int drive = 0;  // Drive A:

//     DirectoryEntry* root_directory = (DirectoryEntry*)malloc(sizeof(DirectoryEntry) * fat12.bootSector.rootEntryCount);
//     if (!root_directory) {
//         printf("Memory allocation error for root directory.\n");
//         return false;
//     }

//     // Read the Root Directory into a local buffer
//     if (!read_root_directory(drive, root_directory)) {
//         printf("Error reading the Root Directory.\n");
//         free(root_directory);
//         return false;
//     }

//     // Display the Root Directory
//     display_root_directory(root_directory, fat12.bootSector.rootEntryCount);
//     free(root_directory);
//     return true;
// }
