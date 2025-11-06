#include "fat32.h"
#include "lib/libc/stdio.h"

// fat32 file system implementation
// The file is divided into:
// fat32.c: Contains the implementation of the FAT32 filesystem functions.
// fat32_dir.c: Contains the implementation of the FAT32 directory functions.
// fat32_files.c: Contains the implementation of the FAT32 file functions.
// fat32_cluster.c: Contains the implementation of the FAT32 I/O functions.

unsigned int current_directory_cluster = 2; // Default root directory cluster for FAT32
struct fat32_boot_sector boot_sector;

unsigned short ata_base_address;
bool ata_is_master;

int fat32_init_fs(unsigned short base, bool is_master) {
    // Read the first sector (LBA 0) into boot_sector
    if (!ata_read_sector(base, 0, &boot_sector, is_master)) {
        printf("+++ Error reading boot sector +++.\n");
        return FAILURE;
    }

    // Debug: Dump first 32 bytes of boot sector
    printf("Boot sector first 32 bytes:\n");
    uint8_t* bs_bytes = (uint8_t*)&boot_sector;
    for (int i = 0; i < 32; i++) {
        printf("%02X ", bs_bytes[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    
    // Debug: Show parsed values
    printf("Parsed boot sector:\n");
    printf("  bytesPerSector: %u (offset 11-12)\n", boot_sector.bytes_per_sector);
    printf("  sectorsPerCluster: %u (offset 13)\n", boot_sector.sectors_per_cluster);
    printf("  reservedSectorCount: %u (offset 14-15)\n", boot_sector.reserved_sector_count);
    printf("  numberOfFATs: %u (offset 16)\n", boot_sector.number_of_fats);
    printf("  rootEntryCount: %u (offset 17-18)\n", boot_sector.root_entry_count);
    printf("  FATSize32: %u (offset 36-39)\n", boot_sector.fat_size_32);
    printf("  rootCluster: %u (offset 44-47)\n", boot_sector.root_cluster);
    printf("  bootSignature: 0x%02X (offset 66)\n", boot_sector.boot_signature);
    
    // Check boot signature at offset 510-511
    uint8_t* sector_end = bs_bytes + 510;
    printf("  Sector signature at 510-511: 0x%02X%02X\n", sector_end[1], sector_end[0]);

#ifdef FAT32_STRICT_VALIDATION
    // Real hardware: Strict validation
    if (boot_sector.boot_signature != 0xAA55) {
        printf("+++ Invalid boot signature: 0x%04X +++\n", boot_sector.boot_signature);
        return FAILURE;
    }
    
    if (boot_sector.bytes_per_sector != 512 && boot_sector.bytes_per_sector != 1024 &&
        boot_sector.bytes_per_sector != 2048 && boot_sector.bytes_per_sector != 4096) {
        printf("+++ Invalid bytes per sector: %u +++\n", boot_sector.bytes_per_sector);
        return FAILURE;
    }
    
    if (boot_sector.sectors_per_cluster == 0 || 
        (boot_sector.sectors_per_cluster & (boot_sector.sectors_per_cluster - 1)) != 0) {
        printf("+++ Invalid sectors per cluster: %u +++\n", boot_sector.sectors_per_cluster);
        return FAILURE;
    }
    
    if (boot_sector.root_cluster < 2) {
        printf("+++ Invalid root cluster: %u +++\n", boot_sector.root_cluster);
        return FAILURE;
    }
    
    printf("FAT32 (strict): root=%u, bps=%u, spc=%u\n",
           boot_sector.root_cluster, boot_sector.bytes_per_sector, boot_sector.sectors_per_cluster);
#else
    // QEMU: Basic validation only
    printf("FAT32 (relaxed): root=%u\n", boot_sector.root_cluster);
#endif

    ata_is_master = is_master;
    ata_base_address = base;
    current_directory_cluster = boot_sector.root_cluster;
    
    printf("FAT32 init complete, returning SUCCESS\n");
    return SUCCESS;
}

// ------------------------------------------------------------------
// Compares a FAT32 8.3 formatted name with a regular string
// Returns 0 if the names match, -1 otherwise
// ------------------------------------------------------------------
int compare_names(const char* fat_name, const char* regular_name) {
    int i, j;

    // Compare the name part (first 8 characters)
    for (i = 0, j = 0; i < 8; i++) {
        if (regular_name[j] == '\0' || regular_name[j] == '.') {
            // If we reached the end of the regular name or a dot, the rest of the fat_name should be spaces
            if (fat_name[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (toupper(fat_name[i]) != toupper(regular_name[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If the regular name has a dot for the extension, skip it
    if (regular_name[j] == '.') {
        j++;
    }

    // Compare the extension part (last 3 characters)
    for (i = 8; i < 11; i++) {
        if (regular_name[j] == '\0') {
            // If we reached the end of the regular name, the rest of the fat_name should be spaces
            if (fat_name[i] != ' ') {
                return -1; // Not a match
            }
        } else {
            // Compare characters (case-insensitive)
            if (toupper(fat_name[i]) != toupper(regular_name[j])) {
                return -1; // Not a match
            }
            j++;
        }
    }

    // If we reached the end of both strings without a mismatch, it's a match
    return (regular_name[j] == '\0') ? 0 : -1;
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

unsigned int read_fat_entry(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    unsigned int fat_offset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fat_sector = boot_sector->reserved_sector_count + (fat_offset / boot_sector->bytes_per_sector);
    unsigned int ent_offset = fat_offset % boot_sector->bytes_per_sector;
    // Buffer to read a part of the FAT
    unsigned char buffer[boot_sector->bytes_per_sector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(ata_base_address, fat_sector, buffer, ata_is_master)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return INVALID_CLUSTER;
    }
    // Read the 4 bytes of the current cluster's entry from the buffer
    unsigned int next_cluster = *(unsigned int*)&buffer[ent_offset];
    // Mask out the high 4 bits (reserved for FAT32)
    next_cluster &= 0x0FFFFFFF;
    return next_cluster;
}

bool write_fat_entry(struct fat32_boot_sector* boot_sector, unsigned int cluster, unsigned int value) {
    unsigned int fat_offset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fat_sector = boot_sector->reserved_sector_count + (fat_offset / boot_sector->bytes_per_sector);
    unsigned int ent_offset = fat_offset % boot_sector->bytes_per_sector;
    // Buffer to read and modify a part of the FAT
    unsigned char buffer[boot_sector->bytes_per_sector];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!ata_read_sector(ata_base_address, fat_sector, buffer, ata_is_master)) {
        // Handle read error
        printf("Error: Failed to read the sector containing the FAT entry.\n");
        return false;
    }
    // Modify the FAT entry in the buffer
    unsigned int* fat_entry = (unsigned int*)&buffer[ent_offset];
    *fat_entry = (*fat_entry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve high 4 bits, modify the rest
    // Write the modified sector back to the FAT
    if (!ata_write_sector(ata_base_address, fat_sector, buffer, ata_is_master)) {
        // Handle write error
        printf("Error: Failed to write the modified sector back to the FAT.\n");
        return false;
    }
    return true;
}

bool remove_entry_from_directory(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, struct fat32_dir_entry* entry) {
    unsigned int entries_per_cluster = get_entries_per_cluster(boot_sector);
    struct fat32_dir_entry entries[entries_per_cluster];
    unsigned int current_cluster = parent_cluster;

    while (current_cluster < FAT32_EOC_MIN) {
        read_cluster(boot_sector, current_cluster, entries);

        for (unsigned int i = 0; i < entries_per_cluster; ++i) {
            if (memcmp(&entries[i], entry, sizeof(struct fat32_dir_entry)) == 0) {
                memset(&entries[i], 0, sizeof(struct fat32_dir_entry));
                write_cluster(boot_sector, current_cluster, entries);
                return true;
            }
        }

        current_cluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    }

    return false;
}

void ata_debug_bootsector(drive_t* drive) {
    printf("=== ATA Debug: Boot Sector ===\n");
    printf("Drive: %s\n", drive->name);
    printf("Base: 0x%X, is_master: %d\n", drive->base, drive->is_master);
    
    // Read the boot sector from the ATA drive
    if (!ata_read_sector(drive->base, 0, &boot_sector, drive->is_master)) {
        printf("+++ Error reading boot sector +++.\n");
        return;
    }

    printf("\nBoot sector read successful!\n");
    printf("Bytes per sector: %u\n", boot_sector.bytes_per_sector);
    printf("Sectors per cluster: %u\n", boot_sector.sectors_per_cluster);
    printf("Reserved sectors: %u\n", boot_sector.reserved_sector_count);
    printf("Number of FATs: %u\n", boot_sector.number_of_fats);
    printf("Root entry count: %u\n", boot_sector.root_entry_count);
    printf("FAT size (32-bit): %u\n", boot_sector.fat_size_32);
    printf("Root cluster: %u\n", boot_sector.root_cluster);
    printf("Boot signature: 0x%02X\n", boot_sector.boot_signature);
    
    // Check signature at offset 510-511
    uint8_t* bs_bytes = (uint8_t*)&boot_sector;
    printf("Sector signature at 510-511: 0x%02X%02X\n", bs_bytes[511], bs_bytes[510]);
    
    // Manual hex dump of first 64 bytes (avoid hex_dump with wait_enter_pressed)
    printf("\nFirst 64 bytes:\n");
    for (int i = 0; i < 64; i++) {
        printf("%02X ", bs_bytes[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    // Manual hex dump of last 32 bytes
    printf("\nLast 32 bytes (offset 480-511):\n");
    for (int i = 480; i < 512; i++) {
        printf("%02X ", bs_bytes[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    
    printf("==============================\n");
}