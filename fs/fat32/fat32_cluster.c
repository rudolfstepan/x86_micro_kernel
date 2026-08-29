/**
 * @file fs/fat32/fat32_cluster.c
 * @brief FAT32-Clusterallokation und FAT-Spiegelpflege.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Zyklische oder außerhalb liegende Clusterketten werden abgebrochen.
 */
#include "fat32.h"
#include "lib/libc/stdio.h"
#include "drivers/bus/drives.h"

// --------------------------------------------------------------------
// is_valid_cluster
// Validates that a cluster number is within valid range
// --------------------------------------------------------------------
bool is_valid_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    if (!boot_sector || cluster < 2 || cluster >= FAT32_RESERVED_MIN) {
        return false;
    }

    uint32_t count = get_total_clusters(boot_sector);
    return count != 0 && (uint64_t)cluster <= (uint64_t)count + 1u;
}

// --------------------------------------------------------------------
// get_entries_per_cluster
// Calculates the number of directory entries that can fit in a cluster
// --------------------------------------------------------------------
unsigned int get_entries_per_cluster(struct fat32_boot_sector* boot_sector) {
    // Safety check to prevent divide by zero
    if (boot_sector->bytes_per_sector == 0 || boot_sector->sectors_per_cluster == 0) {
        printf("Error: Invalid boot sector values (bytes_per_sector=%u, sectors_per_cluster=%u)\n",
               boot_sector->bytes_per_sector, boot_sector->sectors_per_cluster);
        return 1; // Return minimum safe value
    }
    
    unsigned int cluster_size = boot_sector->bytes_per_sector * boot_sector->sectors_per_cluster;
    unsigned int entries_per_cluster = cluster_size / DIRECTORY_ENTRY_SIZE;

    return entries_per_cluster;
}

// --------------------------------------------------------------------
// get_total_clusters
// Calculates the total number of clusters in the filesystem
// --------------------------------------------------------------------
unsigned int get_total_clusters(struct fat32_boot_sector* boot_sector) {
    // Safety check to prevent divide by zero
    if (boot_sector->sectors_per_cluster == 0) {
        printf("Error: sectors_per_cluster is zero in get_total_clusters\n");
        return 2; // Return minimum safe value (clusters start at 2)
    }
    
    // Assuming you have a global or accessible boot_sector structure
    // and the structure has fields: totalSectors32, reservedSectorCount,
    // numberOfFATs, FATsizeFAT32, and sectorsPerCluster
    uint64_t metadata = (uint64_t)boot_sector->reserved_sector_count +
                        (uint64_t)boot_sector->number_of_fats *
                        boot_sector->fat_size_32;
    if (metadata >= boot_sector->total_sectors_32) {
        return 0;
    }

    unsigned int total_data_sectors =
        boot_sector->total_sectors_32 - (uint32_t)metadata;

    unsigned int total_clusters = total_data_sectors / boot_sector->sectors_per_cluster;

    return total_clusters;
}

// --------------------------------------------------------------------
// find_free_cluster
// Finds the first free cluster in the filesystem
// Returns the cluster number if found, or INVALID_CLUSTER if not found
// Uses FSInfo hint if available for faster allocation
// --------------------------------------------------------------------
unsigned int find_free_cluster(struct fat32_boot_sector* boot_sector) {
    extern struct fat32_fsinfo fsinfo;
    extern bool fsinfo_valid;
    
    unsigned int total_clusters = get_total_clusters(boot_sector);
    unsigned int start_cluster = 2;
    
    // Use FSInfo hint if available
    if (fsinfo_valid && fsinfo.next_free_cluster != 0xFFFFFFFF && 
        fsinfo.next_free_cluster >= 2 &&
        fsinfo.next_free_cluster <= total_clusters + 1u) {
        start_cluster = fsinfo.next_free_cluster;
    }
    
    // Search from hint to end
    for (unsigned int cluster = start_cluster; cluster <= total_clusters + 1u; cluster++) {
        if (read_fat_entry(boot_sector, cluster) == 0) {
            // Found a free cluster - update FSInfo hint
            if (fsinfo_valid) {
                fsinfo.next_free_cluster =
                    cluster < total_clusters + 1u ? cluster + 1u : 0xFFFFFFFF;
            }
            return cluster;
        }
    }
    
    // Wrap around: search from beginning to hint
    if (start_cluster > 2) {
        for (unsigned int cluster = 2; cluster < start_cluster; cluster++) {
            if (read_fat_entry(boot_sector, cluster) == 0) {
                // Found a free cluster - update FSInfo hint
                if (fsinfo_valid) {
                    fsinfo.next_free_cluster = cluster + 1u;
                }
                return cluster;
            }
        }
    }

    return INVALID_CLUSTER; // No free cluster found
}

// --------------------------------------------------------------------
// mark_cluster_in_fat
// --------------------------------------------------------------------
static bool write_fat_copy_entry(struct fat32_boot_sector* boot_sector,
                                 unsigned int fat_number,
                                 unsigned int fat_offset,
                                 unsigned int value) {
    const unsigned int sector = partition_lba_offset +
        boot_sector->reserved_sector_count +
        fat_number * boot_sector->fat_size_32 +
        fat_offset / boot_sector->bytes_per_sector;
    const unsigned int entry_offset = fat_offset % boot_sector->bytes_per_sector;
    const unsigned int desired = value & 0x0FFFFFFFu;
    unsigned char buffer[SECTOR_SIZE];
    unsigned char verify[SECTOR_SIZE];
    bool write_reported_success = false;
    bool observed_after_write = false;

    /* A command or its immediate verify read can fail after the sector was
     * committed. Resolve the actual entry and retry before returning false. */
    for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        if (!ata_read_sector(ata_base_address, sector, buffer,
                             ata_is_master)) {
            continue;
        }

        unsigned int* entry = (unsigned int*)&buffer[entry_offset];
        if ((*entry & 0x0FFFFFFFu) == desired) return true;
        *entry = (*entry & 0xF0000000u) | desired;
        if (fat32_write_sector(sector, buffer)) {
            write_reported_success = true;
        }

        for (unsigned int verify_attempt = 0; verify_attempt < 3;
             ++verify_attempt) {
            if (ata_read_sector(ata_base_address, sector, verify,
                                ata_is_master)) {
                observed_after_write = true;
                if (((*(unsigned int*)&verify[entry_offset]) & 0x0FFFFFFFu) ==
                    desired) {
                    return true;
                }
            }
        }
    }
    /* A completed ATA write is authoritative when every subsequent read was
     * unavailable.  A readable mismatch still fails: that is evidence that
     * the requested value did not persist. */
    if (write_reported_success && !observed_after_write) return true;
    return false;
}

bool mark_cluster_in_fat(struct fat32_boot_sector* boot_sector,
                         unsigned int cluster, unsigned int value) {
    if (!is_valid_cluster(boot_sector, cluster) ||
        boot_sector->bytes_per_sector != SECTOR_SIZE ||
        boot_sector->number_of_fats == 0) {
        return false;
    }

    const unsigned int active_fat = fat32_active_fat_index(boot_sector);
    if (active_fat >= boot_sector->number_of_fats) return false;

    const unsigned int fat_offset = cluster * 4u;
    const unsigned int active_sector = partition_lba_offset +
        boot_sector->reserved_sector_count +
        active_fat * boot_sector->fat_size_32 +
        fat_offset / boot_sector->bytes_per_sector;
    const unsigned int entry_offset = fat_offset % boot_sector->bytes_per_sector;
    unsigned char active_buffer[SECTOR_SIZE];
    if (!ata_read_sector(ata_base_address, active_sector, active_buffer,
                         ata_is_master)) {
        return false;
    }
    const unsigned int old_value =
        (*(unsigned int*)&active_buffer[entry_offset]) & 0x0FFFFFFFu;

    const unsigned int first_fat = fat32_fat_is_mirrored(boot_sector) ?
        0u : active_fat;
    const unsigned int fat_count = fat32_fat_is_mirrored(boot_sector) ?
        boot_sector->number_of_fats : active_fat + 1u;
    bool mirror_degraded = false;
    for (unsigned int fat_number = first_fat; fat_number < fat_count;
         ++fat_number) {
        bool updated = write_fat_copy_entry(boot_sector, fat_number,
                                            fat_offset, value);
        if (fat_number == active_fat && !updated) {
            printf("Error: Failed to confirm active FAT entry %u\n", cluster);
            return false;
        }
        if (!updated) {
            mirror_degraded = true;
            printf("Warning: FAT copy %u is stale at entry %u\n",
                   fat_number, cluster);
        }
    }

    extern void update_fsinfo_free_count(int delta);
    if (old_value == 0 && value != 0) {
        update_fsinfo_free_count(-1);
    } else if (old_value != 0 && value == 0) {
        update_fsinfo_free_count(1);
    }
    if (mirror_degraded) {
        printf("Warning: continuing with the verified active FAT copy\n");
    }
    return true;
}

// --------------------------------------------------------------------
// get_first_data_sector
// Calculates the first sector of the data region in the filesystem
// --------------------------------------------------------------------
unsigned int get_first_data_sector(struct fat32_boot_sector* boot_sector) {
    if (!boot_sector || boot_sector->bytes_per_sector == 0) return INVALID_CLUSTER;
    uint64_t root_dir_sectors =
        ((uint64_t)boot_sector->root_entry_count * 32u +
         boot_sector->bytes_per_sector - 1u) / boot_sector->bytes_per_sector;
    uint64_t first = (uint64_t)partition_lba_offset +
        boot_sector->reserved_sector_count +
        (uint64_t)boot_sector->number_of_fats * boot_sector->fat_size_32 +
        root_dir_sectors;
    return first <= UINT32_MAX ? (uint32_t)first : INVALID_CLUSTER;
}

// --------------------------------------------------------------------
// write_cluster
// Writes the specified entries to the specified cluster
// Returns true if successful, false otherwise
// --------------------------------------------------------------------
bool write_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster, const struct fat32_dir_entry* entries) {
    if (entries == NULL || !is_valid_cluster(boot_sector, cluster) ||
        boot_sector->bytes_per_sector != SECTOR_SIZE) {
        printf("Error: Entries buffer is null.\n");
        return false; // Error: Buffer is null
    }
    unsigned int first_sector_of_cluster =
        cluster_to_sector(boot_sector, cluster);
    if (first_sector_of_cluster == INVALID_CLUSTER) return false;

    for (uint32_t offset = 0U; offset < boot_sector->sectors_per_cluster;) {
        uint32_t remaining = boot_sector->sectors_per_cluster - offset;
        uint32_t count = remaining > ATA_PIO_MAX_SECTORS
            ? ATA_PIO_MAX_SECTORS : remaining;
        uint32_t sector_number = first_sector_of_cluster + offset;
        const uint8_t *buffer = (const uint8_t *)entries +
                                offset * boot_sector->bytes_per_sector;
        if (!ata_write_sectors(ata_base_address, sector_number, count, buffer,
                               ata_is_master)) {
            printf("Error: Failed verified cluster write at sector %u.\n",
                   sector_number);
            return false;
        }
        offset += count;
    }
    return true;
}
// return the start sector of a cluster
unsigned int cluster_to_sector(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    // Validate cluster number
    if (!is_valid_cluster(boot_sector, cluster)) {
        printf("Error: Invalid cluster number %u\n", cluster);
        return INVALID_CLUSTER;
    }
    
    uint64_t first_data_sector = boot_sector->reserved_sector_count +
        ((uint64_t)boot_sector->number_of_fats * boot_sector->fat_size_32);
    uint64_t sector = partition_lba_offset + first_data_sector +
        ((uint64_t)(cluster - 2) * boot_sector->sectors_per_cluster);
    uint64_t volume_end = (uint64_t)partition_lba_offset +
                          boot_sector->total_sectors_32;
    if (sector > UINT32_MAX ||
        sector + boot_sector->sectors_per_cluster > volume_end) {
        return INVALID_CLUSTER;
    }
    return (unsigned int)sector;
}

bool read_cluster(struct fat32_boot_sector* boot_sector, unsigned int cluster_number, void* buffer) {
    if (!buffer || !is_valid_cluster(boot_sector, cluster_number) ||
        boot_sector->bytes_per_sector != SECTOR_SIZE) {
        printf("Error: Cannot read invalid cluster %u\n", cluster_number);
        return false;
    }
    
    unsigned int startSector = cluster_to_sector(boot_sector, cluster_number);
    if (startSector == INVALID_CLUSTER) {
        printf("Error: Invalid sector for cluster %u\n", cluster_number);
        return false;
    }
    
    for (uint32_t offset = 0U; offset < boot_sector->sectors_per_cluster;) {
        uint32_t remaining = boot_sector->sectors_per_cluster - offset;
        uint32_t count = remaining > ATA_PIO_MAX_SECTORS
            ? ATA_PIO_MAX_SECTORS : remaining;
        if (!ata_read_sectors(ata_base_address, startSector + offset, count,
                              (uint8_t *)buffer + offset * SECTOR_SIZE,
                              ata_is_master)) return false;
        offset += count;
    }
    return true;
}

unsigned int read_start_cluster(struct fat32_dir_entry* entry) {
    return ((unsigned int)entry->first_cluster_high << 16) | entry->first_cluster_low;
}

unsigned int get_next_cluster_in_chain(struct fat32_boot_sector* boot_sector, unsigned int current_cluster) {
    if (!is_valid_cluster(boot_sector, current_cluster) ||
        boot_sector->bytes_per_sector != SECTOR_SIZE) {
        return INVALID_CLUSTER;
    }
    return read_fat_entry(boot_sector, current_cluster);
}

bool is_end_of_cluster_chain(unsigned int cluster) {
    return cluster >= FAT32_EOC_MIN && cluster <= FAT32_EOC_MAX;
}

// Function to find the next cluster given a directory name and a starting cluster
unsigned int find_next_cluster(struct fat32_boot_sector* boot_sector, const char* dir_name, unsigned int current_cluster) {
    if (!boot_sector || !dir_name ||
        !is_valid_cluster(boot_sector, current_cluster))
        return INVALID_CLUSTER;
    struct fat32_dir_entry entry;
    if (fat32_lookup_entry_in_directory(current_cluster, dir_name, &entry) !=
            FAT32_LOOKUP_FOUND || !(entry.attr & ATTR_DIRECTORY))
        return INVALID_CLUSTER;
    unsigned int cluster = read_start_cluster(&entry);
    return is_valid_cluster(boot_sector, cluster) ? cluster : INVALID_CLUSTER;
}

// Function to format the filename in the 8.3 format
void format_filename(char *output, unsigned char *name) {
    // Copy the first 8 characters as the filename, trimming trailing spaces
    char filename[9] = {0};  // 8 characters + null terminator
    strncpy(filename, (const char *)name, 8);
    for (int i = 7; i >= 0; i--) {
        if (filename[i] == ' ') {
            filename[i] = '\0';  // Trim trailing spaces
        } else {
            break;
        }
    }

    // Copy the next 3 characters as the extension, trimming trailing spaces
    char extension[4] = {0};  // 3 characters + null terminator
    strncpy(extension, (const char *)(name + 8), 3);
    for (int i = 2; i >= 0; i--) {
        if (extension[i] == ' ') {
            extension[i] = '\0';  // Trim trailing spaces
        } else {
            break;
        }
    }

    // Format the output as 'FILENAME.EXT' or just 'FILENAME' if no extension
    if (strlen(extension) > 0) {
        snprintf(output, 13, "%s.%s", filename, extension);
    } else {
        snprintf(output, 9, "%s", filename);
    }
}

// Function to extract date and time from FAT32 format
void extract_fat32_date(uint16_t fat_date, int* day, int* month, int* year) {
    *day = fat_date & 0x1F;
    *month = (fat_date >> 5) & 0x0F;
    *year = ((fat_date >> 9) & 0x7F) + 1980;
}

void extract_fat32_time(uint16_t fat_time, int* hours, int* minutes, int* seconds) {
    *seconds = (fat_time & 0x1F) * 2;
    *minutes = (fat_time >> 5) & 0x3F;
    *hours = (fat_time >> 11) & 0x1F;
}

// Function to read and print directory entries in a DOS-like format
void read_cluster_dir_entries(unsigned int current_cluster) {
    size_t cluster_bytes = SECTOR_SIZE * boot_sector.sectors_per_cluster;
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(cluster_bytes);
    if (!entries) {
        return;
    }

    // Print DOS-like header
    printf(" Volume in drive C has no label\n");
    printf(" Directory of \\\n\n");
    printf("FILENAME      SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    uint32_t traversed = 0;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    bool end_of_directory = false;
    while (is_valid_cluster(&boot_sector, current_cluster) &&
           traversed++ < cluster_limit && !end_of_directory) {
        if (!read_cluster(&boot_sector, current_cluster, entries)) {
            free(entries);
            return;
        }

        unsigned int count = cluster_bytes / sizeof(struct fat32_dir_entry);
        for (unsigned int j = 0; j < count; j++) {
            if (entries[j].name[0] == 0x00) {
                end_of_directory = true;
                break;
            }

            // Skip LFN entries and deleted entries
            if ((entries[j].name[0] == 0xE5) ||
                (entries[j].attr & 0x0F) == 0x0F) {
                continue;
            }

            char current_name[13];
            format_filename(current_name, entries[j].name);

            int day, month, year, hours, minutes, seconds;
            extract_fat32_date(entries[j].write_date, &day, &month, &year);
            extract_fat32_time(entries[j].write_time, &hours, &minutes, &seconds);

            if (entries[j].attr & 0x10) {
                printf("%-12s   <DIR>          %02d-%02d-%04d  %02d:%02d:%02d\n",
                       current_name, day, month, year, hours, minutes, seconds);
            } else {
                printf("%-12s %10u %02d-%02d-%04d  %02d:%02d:%02d\n",
                       current_name, entries[j].file_size, day, month, year,
                       hours, minutes, seconds);
            }
        }

        if (!end_of_directory) {
            uint32_t next = get_next_cluster_in_chain(&boot_sector, current_cluster);
            if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next)) {
                break;
            }
            current_cluster = next;
        }
    }
    free(entries);
}

unsigned int allocate_new_cluster(struct fat32_boot_sector* boot_sector) {
    if (!boot_sector || get_total_clusters(boot_sector) == 0U) {
        return INVALID_CLUSTER;
    }

    /* `find_free_cluster` validates and consumes the standard FAT32 FSInfo
     * next-free hint before performing its bounded wraparound scan.  Starting
     * every allocation at cluster 2 made sequential growth quadratic on a
     * normally populated system volume even though the valid hint was already
     * available. */
    unsigned int cluster = find_free_cluster(boot_sector);
    if (cluster == INVALID_CLUSTER) return INVALID_CLUSTER;
    if (mark_cluster_in_fat(boot_sector, cluster, FAT32_EOC_MAX)) {
        return cluster;
    }

    /* `find_free_cluster` advances the in-memory hint before publication.  If
     * the active FAT still proves the candidate free, make it the next retry;
     * an ambiguous/non-free observation is never reclaimed or reused here. */
    if (fsinfo_valid && read_fat_entry(boot_sector, cluster) == 0U) {
        fsinfo.next_free_cluster = cluster;
    }
    return INVALID_CLUSTER;
}

bool link_cluster_to_chain(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, unsigned int new_cluster) {
    if (!is_valid_cluster(boot_sector, parent_cluster) ||
        !is_valid_cluster(boot_sector, new_cluster)) {
        return false;
    }

    unsigned int current_cluster = parent_cluster;
    unsigned int nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    unsigned int traversed = 0;
    unsigned int cluster_limit = get_total_clusters(boot_sector);

    while (nextCluster != INVALID_CLUSTER &&
           !is_end_of_cluster_chain(nextCluster) &&
           traversed++ < cluster_limit) {
        if (!is_valid_cluster(boot_sector, nextCluster)) {
            return false;
        }
        current_cluster = nextCluster;
        nextCluster = get_next_cluster_in_chain(boot_sector, current_cluster);
    }
    if (nextCluster == INVALID_CLUSTER || traversed >= cluster_limit) {
        return false;
    }
    // current_cluster now points to the last cluster in the chain
    // Update the FAT to link the new cluster to the chain
    if (!mark_cluster_in_fat(boot_sector, current_cluster, new_cluster)) {
        unsigned int observed =
            get_next_cluster_in_chain(boot_sector, current_cluster);
        if (observed != new_cluster) {
            printf("Error: Failed to confirm cluster %u in chain starting at %u\n",
                   new_cluster, parent_cluster);
            return false;
        }
    }
    return true;
}

bool fat32_get_chain_tail(struct fat32_boot_sector* boot_sector,
                          unsigned int start_cluster,
                          unsigned int* tail_cluster) {
    if (!boot_sector || !tail_cluster ||
        !is_valid_cluster(boot_sector, start_cluster)) {
        return false;
    }

    unsigned int current = start_cluster;
    unsigned int cluster_limit = get_total_clusters(boot_sector);
    for (unsigned int traversed = 0; traversed < cluster_limit; ++traversed) {
        unsigned int next =
            get_next_cluster_in_chain(boot_sector, current);
        if (next == INVALID_CLUSTER) return false;
        if (is_end_of_cluster_chain(next)) {
            *tail_cluster = current;
            return true;
        }
        if (!is_valid_cluster(boot_sector, next)) return false;
        current = next;
    }
    return false;
}

bool fat32_reclaim_chain_suffix(struct fat32_boot_sector* boot_sector,
                                unsigned int original_tail) {
    if (!boot_sector || !is_valid_cluster(boot_sector, original_tail)) {
        return false;
    }

    unsigned int suffix =
        get_next_cluster_in_chain(boot_sector, original_tail);
    if (suffix == INVALID_CLUSTER) return false;
    if (is_end_of_cluster_chain(suffix)) return true;
    if (!is_valid_cluster(boot_sector, suffix)) return false;

    /* Detach first: once the old tail is durably EOC, reclaiming any part of
     * the private suffix can no longer leave the published file dangling. */
    if (!mark_cluster_in_fat(boot_sector, original_tail, FAT32_EOC_MAX)) {
        unsigned int observed =
            get_next_cluster_in_chain(boot_sector, original_tail);
        if (observed == INVALID_CLUSTER ||
            !is_end_of_cluster_chain(observed)) {
            return false;
        }
    }
    return free_cluster_chain(boot_sector, suffix);
}

bool free_cluster_chain(struct fat32_boot_sector* boot_sector, unsigned int start_cluster) {
    // Validate start cluster
    if (!is_valid_cluster(boot_sector, start_cluster)) {
        printf("Error: Invalid start cluster %u for free_cluster_chain\n", start_cluster);
        return false;
    }

    unsigned int current_cluster = start_cluster;
    unsigned int traversed = 0;
    unsigned int cluster_limit = get_total_clusters(boot_sector);

    while (traversed++ < cluster_limit) {
        // Read the successor before freeing the current entry.  A read error
        // must not be confused with EOC, otherwise a partial free is reported
        // as success.
        unsigned int next_cluster = get_next_cluster_in_chain(boot_sector, current_cluster);
        if (next_cluster == INVALID_CLUSTER) {
            return false;
        }

        // Free the current cluster's FAT entry
        if (!mark_cluster_in_fat(boot_sector, current_cluster, 0)) {
            printf("Error: Failed to free cluster %u\n", current_cluster);
            return false;
        }

        // If next_cluster indicates end-of-chain or an error, stop
        if (is_end_of_cluster_chain(next_cluster)) {
            break;
        }

        // If the next cluster is invalid for the filesystem boundaries, stop to avoid loops
        if (!is_valid_cluster(boot_sector, next_cluster)) {
            printf("Warning: Next cluster %u is invalid, stopping free operation\n", next_cluster);
            return false;
        }

        // Continue with the next cluster
        current_cluster = next_cluster;
    }

    return traversed <= cluster_limit;
}
