#include "fat12.h"
#include "drivers/fdd/fdd.h"
#include "toolchain/stdio.h"

#define ROOT_ENTRY_SIZE 32  // Each root directory entry is 32 bytes


fat12_fs_t fat12_fs;

// Function to initialize the FAT12 file system on a floppy disk drive
bool fat12_init_fs() {
    uint8_t boot_sector[SECTOR_SIZE];

    printf("Initializing FAT12 file system...\n");

    // Read the boot sector from the floppy disk (Cylinder 0, Head 0, Sector 1)
    if (!fdd_read_sector(0, 0, 1, boot_sector)) {
        printf("Failed to read boot sector.\n");
        return false;
    }

    // Copy the BPB from the boot sector into our FAT12 structure
    fat12_bpb_t *bpb = (fat12_bpb_t*) boot_sector;
    fat12_fs.bpb = *bpb;

    // Print some information for debugging
    printf("FAT12 Volume Label: %.11s\n", fat12_fs.bpb.volume_label);
    printf("Bytes per Sector: %u\n", fat12_fs.bpb.bytes_per_sector);
    printf("Sectors per Cluster: %u\n", fat12_fs.bpb.sectors_per_cluster);
    printf("Reserved Sectors: %u\n", fat12_fs.bpb.reserved_sectors);
    printf("Number of FATs: %u\n", fat12_fs.bpb.num_fats);
    printf("Root Entry Count: %u\n", fat12_fs.bpb.root_entry_count);
    printf("Total Sectors: %u\n", fat12_fs.bpb.total_sectors_16 ? fat12_fs.bpb.total_sectors_16 : fat12_fs.bpb.total_sectors_32);

    // Calculate FAT start, root directory start, and data area start
    fat12_fs.fat_start = fat12_fs.bpb.reserved_sectors;
    fat12_fs.root_dir_start = fat12_fs.fat_start + (fat12_fs.bpb.num_fats * fat12_fs.bpb.fat_size_16);
    fat12_fs.data_start = fat12_fs.root_dir_start + ((fat12_fs.bpb.root_entry_count * ROOT_ENTRY_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE);

    printf("FAT Start Sector: %u\n", fat12_fs.fat_start);
    printf("Root Directory Start Sector: %u\n", fat12_fs.root_dir_start);
    printf("Data Area Start Sector: %u\n", fat12_fs.data_start);

    return true;
}

// Function to read the contents of a directory in the FAT12 file system
int fat12_read_dir(const char* path, char* buffer, unsigned int* size) {
    printf("Reading FAT12 directory: %s\n", path);

    // Check if the path is the root directory
    if (strcmp(path, "/") == 0) {
        unsigned int root_dir_entries = fat12_fs.bpb.root_entry_count;
        unsigned int root_dir_size = (root_dir_entries * ROOT_ENTRY_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
        unsigned int buffer_offset = 0;

        memset(buffer, 0, *size);

        // Read each sector of the root directory
        for (unsigned int i = 0; i < root_dir_size; i++) {
            unsigned int cylinder = (fat12_fs.root_dir_start + i) / (fat12_fs.bpb.sectors_per_track * fat12_fs.bpb.num_heads);
            unsigned int head = ((fat12_fs.root_dir_start + i) / fat12_fs.bpb.sectors_per_track) % fat12_fs.bpb.num_heads;
            unsigned int sector = (fat12_fs.root_dir_start + i) % fat12_fs.bpb.sectors_per_track + 1;

            if (!fdd_read_sector(cylinder, head, sector, buffer + buffer_offset)) {
                printf("Failed to read sector %u of root directory.\n", fat12_fs.root_dir_start + i);
                return false;
            }
            buffer_offset += SECTOR_SIZE;
        }

        // Process directory entries
        *size = 0;
        for (unsigned int i = 0; i < root_dir_entries; i++) {
            char* entry = buffer + (i * ROOT_ENTRY_SIZE);

            if ((unsigned char)entry[0] == 0x00 || (unsigned char)entry[0] == 0xE5) {
                continue;
            }

            char name[9] = {0};
            char ext[4] = {0};
            strncpy(name, entry, 8);
            strncpy(ext, entry + 8, 3);

            for (int j = 7; j >= 0 && name[j] == ' '; j--) {
                name[j] = '\0';
            }
            for (int j = 2; j >= 0 && ext[j] == ' '; j--) {
                ext[j] = '\0';
            }

            uint8_t attributes = (uint8_t)entry[11];
            if (attributes & 0x10) {
                if (name[0] != '.' || (name[1] != '\0' && !(name[1] == '.' && name[2] == '\0'))) {
                    printf("Directory: %s\n", name);
                    sprintf(buffer + *size, "%s/\n", name);
                    *size += strlen(name) + 2;
                }
            } else if (attributes & 0x08) {
                continue;
            } else {
                if (strlen(ext) > 0) {
                    printf("File: %s.%s\n", name, ext);
                    sprintf(buffer + *size, "%s.%s\n", name, ext);
                    *size += strlen(name) + strlen(ext) + 2;
                } else {
                    printf("File: %s\n", name);
                    sprintf(buffer + *size, "%s\n", name);
                    *size += strlen(name) + 1;
                }
            }
        }
    } else {
        printf("Directory not found: %s\n", path);
        return false;
    }

    return true;
}