#include "fat32.h"
#include "lib/libc/stdio.h"

// fat32 file system implementation
// The file is divided into:
// fat32.c: Contains the implementation of the FAT32 filesystem functions.
// fat32_dir.c: Contains the implementation of the FAT32 directory functions.
// fat32_files.c: Contains the implementation of the FAT32 file functions.
// fat32_cluster.c: Contains the implementation of the FAT32 I/O functions.

unsigned int current_directory_cluster = 2; // Default root directory cluster for FAT32
struct Fat32BootSector boot_sector;

unsigned short ata_base_address;
bool ata_is_master;

int fat32_init_fs(unsigned short base, bool is_master) {
    // Read the first sector (LBA 0) into boot_sector
    if (!ata_read_sector(base, 0, &boot_sector, is_master)) {
        // Handle error (e.g., log error or halt)
        printf("+++ Error reading boot sector +++.\n");

        return FAILURE;
    }

    ata_is_master = is_master;
    ata_base_address = base; // set base address to current drive

    // Update current directory cluster based on the boot sector
    current_directory_cluster = boot_sector.rootCluster;
    return SUCCESS;
}

// formatFilename function
void formatFilename(char* dest, unsigned char* src) {
    int i, j;

    // Copy filename (up to 8 characters, stop at space)
    for (i = 0, j = 0; i < 8 && src[i] != ' '; i++, j++) {
        dest[j] = src[i];
    }

    // If there's an extension, add a dot and copy the extension
    if (src[8] != ' ') {
        dest[j++] = '.';
        for (i = 8; i < 11 && src[i] != ' '; i++, j++) {
            dest[j] = src[i];
        }
    }
    dest[j] = '\0'; // Null-terminate the string
}

// ------------------------------------------------------------------
// Compares a FAT32 8.3 formatted name with a regular string
// Returns 0 if the names match, -1 otherwise
// ------------------------------------------------------------------
int compare_names(const char* fatName, const char* regularName) {
    int i, j;

    // Compare the name part (first 8 characters)
    for (i = 0, j = 0; i < 8; i++) {
        if (regularName[j] == '\0' || regularName[j] == '.') {
            // If we reached the end of the regular name or a dot, the rest of the fatName should be spaces
            if (fatName[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (toupper(fatName[i]) != toupper(regularName[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If the regular name has a dot for the extension, skip it
    if (regularName[j] == '.') {
        j++;
    }

    // Compare the extension part (last 3 characters)
    for (i = 8; i < 11; i++) {
        if (regularName[j] == '\0') {
            // If we reached the end of the regular name, the rest of the fatName should be spaces
            if (fatName[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (toupper(fatName[i]) != toupper(regularName[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If we reached the end of both strings without a mismatch, it's a match
    return (regularName[j] == '\0') ? 0 : -1;
}

void convert_to_83_format(unsigned char* dest, const char* src) {
    // Clear the destination array
    memset(dest, ' ', 11); // 8 chars for name, 3 for extension

    // Special handling for "." and ".." entries
    if (strcmp(src, ".") == 0) {
        dest[0] = '.';
        return;
    } else if (strcmp(src, "..") == 0) {
        dest[0] = '.';
        dest[1] = '.';
        return;
    }

    // Copy the name part
    int i = 0, j = 0;
    while (src[i] != '\0' && src[i] != '.' && j < 8) {
        if (isalnum((unsigned char)src[i])) {
            dest[j++] = toupper((unsigned char)src[i]);
        }
        i++;
    }

    // Skip to the extension part
    while (src[i] != '\0' && src[i] != '.') {
        i++;
    }

    // Copy the extension part
    if (src[i] == '.') {
        i++;
        j = 8; // Extension starts at the 9th position
        while (src[i] != '\0' && j < 11) {
            if (isalnum((unsigned char)src[i])) {
                dest[j++] = toupper((unsigned char)src[i]);
            }
            i++;
        }
    }
}

void set_fat32_time(unsigned short* time, unsigned short* date) {
    // Fixed date and time for testing
    // Let's set the date to 2024-01-11 and time to 12:00:00
    // Year = 2024 - 1980, Month = 1, Day = 11
    // Hour = 12, Minute = 0, Second = 0
    if (time) {
        *time = (12 << 11) | (0 << 5) | (0 / 2);
    }
    if (date) {
        *date = ((2024 - 1980) << 9) | (1 << 5) | 11;
    }
}

unsigned int read_fat_entry(struct Fat32BootSector* boot_sector, unsigned int cluster) {
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = boot_sector->reservedSectorCount + (fatOffset / boot_sector->bytesPerSector);
    unsigned int entOffset = fatOffset % boot_sector->bytesPerSector;
    // Buffer to read a part of the FAT
    unsigned char buffer[boot_sector->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(ata_base_address, fatSector, buffer, ata_is_master)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return INVALID_CLUSTER;
    }
    // Read the 4 bytes of the current cluster's entry from the buffer
    unsigned int nextCluster = *(unsigned int*)&buffer[entOffset];
    // Mask out the high 4 bits (reserved for FAT32)
    nextCluster &= 0x0FFFFFFF;
    return nextCluster;
}

bool write_fat_entry(struct Fat32BootSector* boot_sector, unsigned int cluster, unsigned int value) {
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fatSector = boot_sector->reservedSectorCount + (fatOffset / boot_sector->bytesPerSector);
    unsigned int entOffset = fatOffset % boot_sector->bytesPerSector;
    // Buffer to read and modify a part of the FAT
    unsigned char buffer[boot_sector->bytesPerSector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(ata_base_address, fatSector, buffer, ata_is_master)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false;
    }
    // Modify the FAT entry in the buffer
    unsigned int* fatEntry = (unsigned int*)&buffer[entOffset];
    *fatEntry = (*fatEntry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    // Write the modified sector back to the FAT
    if (!ata_write_sector(ata_base_address, fatSector, buffer, ata_is_master)) {
        // Handle write error
        printf("Error: Failed to write the modified sector back to the FAT.\n");
        return false;
    }
    return true;
}

bool remove_entry_from_directory(struct Fat32BootSector* boot_sector, unsigned int parentCluster, struct FAT32DirEntry* entry) {
    unsigned int entriesPerCluster = get_entries_per_cluster(boot_sector);
    struct FAT32DirEntry entries[entriesPerCluster];
    unsigned int currentCluster = parentCluster;

    while (currentCluster < FAT32_EOC_MIN) {
        read_cluster(boot_sector, currentCluster, entries);

        for (unsigned int i = 0; i < entriesPerCluster; ++i) {
            if (memcmp(&entries[i], entry, sizeof(struct FAT32DirEntry)) == 0) {
                memset(&entries[i], 0, sizeof(struct FAT32DirEntry));
                write_cluster(boot_sector, currentCluster, entries);
                return true;
            }
        }

        currentCluster = get_next_cluster_in_chain(boot_sector, currentCluster);
    }

    return false;
}

void ata_debug_bootsector(drive_t* drive) {

    // Read the boot sector from the ATA drive
    if (!ata_read_sector(drive->base, 0, &boot_sector, true)) {
        // Handle error (e.g., log error or halt)
        printf("+++ Error reading boot sector +++.\n");
        return;
    }

    printf("ATA Debug: Boot Sector\n");
    printf("Bytes per sector: %u\n", boot_sector.bytesPerSector);
    printf("Sectors per cluster: %u\n", boot_sector.sectorsPerCluster);

    hex_dump((unsigned char*)&boot_sector, sizeof(struct Fat32BootSector));
}