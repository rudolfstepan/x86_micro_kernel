/**
 * @file fs/fat32/fat32_files.c
 * @brief FAT32-Datei-, Clusterketten- und Verzeichnisaktualisierung.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Mutationen folgen validierten Clusterketten und geordnetem Metadatencommit.
 */
#include "fat32.h"
#include "include/reist/utf.h"
#include "include/reist/unicode_norm.h"
#include "lib/libc/stdio.h"

// Function to read a file's data into a buffer
// void read_file_data(unsigned int start_cluster, char* buffer, unsigned int size) {
//     unsigned int current_cluster = start_cluster;
//     unsigned int bytes_read = 0;
//     while (bytes_read < size) {
//         unsigned int sector_number = cluster_to_sector(&boot_sector, current_cluster);
//         // Read each sector in the current cluster
//         for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
//             ata_read_sector(current_drive->base, sector_number + i, buffer + bytes_read, current_drive->is_master);
//             bytes_read += SECTOR_SIZE;
//             if (bytes_read >= size) {
//                 break;  // Stop if we have read the required size
//             }
//         }
//         // Get the next cluster in the chain
//         current_cluster = get_next_cluster_in_chain(&boot_sector, current_cluster);
//         // Check if we have reached the end of the file
//         if (is_end_of_cluster_chain(current_cluster)) {
//             break;
//         }
//     }
// }

static void fat32_read_cursor_invalidate(fat32_read_cursor_t* cursor) {
    if (!cursor) return;
    cursor->chain_start = INVALID_CLUSTER;
    cursor->cluster = INVALID_CLUSTER;
    cursor->cluster_index = 0U;
    cursor->next_offset = 0U;
    cursor->valid = false;
}

static bool fat32_read_cursor_matches(const fat32_read_cursor_t* cursor,
                                      uint32_t chain_start, uint32_t offset,
                                      uint32_t cluster_size) {
    if (!cursor || !cursor->valid || cluster_size == 0U ||
        cursor->chain_start != chain_start || cursor->next_offset != offset ||
        !is_valid_cluster(&boot_sector, cursor->cluster)) return false;
    uint32_t target_index = offset / cluster_size;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    return cursor->cluster_index < cluster_limit &&
           target_index >= cursor->cluster_index &&
           target_index - cursor->cluster_index <= 1U;
}

unsigned int read_file_data_at_cursor(unsigned int start_cluster,
                                      unsigned int offset, char* buffer,
                                      unsigned int buffer_size,
                                      unsigned int bytes_to_read,
                                      fat32_read_cursor_t* cursor) {
    if (!buffer || buffer_size == 0 || bytes_to_read == 0 ||
        boot_sector.sectors_per_cluster == 0 ||
        !is_valid_cluster(&boot_sector, start_cluster)) {
        fat32_read_cursor_invalidate(cursor);
        return 0;
    }

    if (bytes_to_read > buffer_size) {
        bytes_to_read = buffer_size;
    }
    if (offset > UINT32_MAX - bytes_to_read) {
        fat32_read_cursor_invalidate(cursor);
        return 0;
    }

    uint32_t cluster_size = SECTOR_SIZE * boot_sector.sectors_per_cluster;
    uint32_t clusters_to_skip = offset / cluster_size;
    uint32_t offset_in_cluster = offset % cluster_size;
    uint32_t current_cluster = start_cluster;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    if (cluster_size == 0U || clusters_to_skip >= cluster_limit) {
        fat32_read_cursor_invalidate(cursor);
        return 0;
    }

    uint32_t current_index = 0U;
    if (fat32_read_cursor_matches(cursor, start_cluster, offset,
                                  cluster_size)) {
        current_cluster = cursor->cluster;
        current_index = cursor->cluster_index;
    } else {
        fat32_read_cursor_invalidate(cursor);
    }

    for (uint32_t i = current_index; i < clusters_to_skip; i++) {
        uint32_t next = get_next_cluster_in_chain(&boot_sector, current_cluster);
        if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next) ||
            !is_valid_cluster(&boot_sector, next)) {
            fat32_read_cursor_invalidate(cursor);
            return 0;
        }
        current_cluster = next;
        current_index++;
    }

    uint32_t total = 0;
    uint32_t traversed = 0U;
    uint8_t sector_buffer[SECTOR_SIZE];
    while (total < bytes_to_read && traversed++ < cluster_limit &&
           is_valid_cluster(&boot_sector, current_cluster)) {
        uint32_t first_sector = cluster_to_sector(&boot_sector, current_cluster);
        if (first_sector == INVALID_CLUSTER) {
            break;
        }

        uint32_t sector_index = offset_in_cluster / SECTOR_SIZE;
        uint32_t sector_offset = offset_in_cluster % SECTOR_SIZE;
        for (uint32_t i = sector_index;
             i < boot_sector.sectors_per_cluster && total < bytes_to_read;
             i++) {
            if (!ata_read_sector(ata_base_address, first_sector + i,
                                 sector_buffer, ata_is_master)) {
                fat32_read_cursor_invalidate(cursor);
                return total;
            }

            uint32_t available = SECTOR_SIZE - sector_offset;
            uint32_t amount = bytes_to_read - total;
            if (amount > available) {
                amount = available;
            }
            memcpy(buffer + total, sector_buffer + sector_offset, amount);
            total += amount;
            sector_offset = 0;
        }

        if (total >= bytes_to_read) {
            break;
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, current_cluster);
        if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next) ||
            !is_valid_cluster(&boot_sector, next)) {
            break;
        }
        current_cluster = next;
        current_index++;
        offset_in_cluster = 0;
    }
    if (cursor && total == bytes_to_read) {
        cursor->chain_start = start_cluster;
        cursor->cluster = current_cluster;
        cursor->cluster_index = current_index;
        cursor->next_offset = offset + total;
        cursor->valid = true;
    } else if (total != bytes_to_read) {
        fat32_read_cursor_invalidate(cursor);
    }
    return total;
}

unsigned int read_file_data_at(unsigned int start_cluster, unsigned int offset,
                               char* buffer, unsigned int buffer_size,
                               unsigned int bytes_to_read) {
    return read_file_data_at_cursor(start_cluster, offset, buffer,
                                    buffer_size, bytes_to_read, NULL);
}

unsigned int read_file_data(unsigned int start_cluster, char* buffer,
                            unsigned int buffer_size,
                            unsigned int bytes_to_read) {
    return read_file_data_at(start_cluster, 0, buffer, buffer_size,
                             bytes_to_read);
}

int read_file_data_to_address(unsigned int start_cluster, void* load_address, unsigned int file_size) {
    // Safety checks
    if (file_size == 0) {
        return 0; // Empty file
    }
    
    if (boot_sector.sectors_per_cluster == 0) {
        printf("Error: sectors_per_cluster is zero\n");
        return 0;
    }
    
    if (!load_address) {
        return 0;
    }

    // Use the partial-sector-aware reader so the final ATA sector cannot
    // overwrite memory beyond the caller's file-sized destination.
    return (int)read_file_data_at(start_cluster, 0, (char*)load_address,
                                  file_size, file_size);
}

static int fat32_load_file_unlocked(const char* filename, void* load_address) {
    // Safety check: ensure boot sector is initialized
    if (boot_sector.bytes_per_sector == 0 || boot_sector.sectors_per_cluster == 0) {
        printf("Error: Filesystem not properly initialized\n");
        return 0;
    }
    
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL) {
        printf("File %s not found for loading into buffer.\n", filename);
        return 0; // we return 0 if the file was not found which is the size of the file
    }
    
    unsigned int file_size = entry->file_size;
    unsigned int start_cluster = read_start_cluster(entry);
    
    // Safety check: validate cluster number
    if (start_cluster < 2) {
        printf("Error: Invalid start cluster %u\n", start_cluster);
        free(entry);
        return 0;
    }
    
    // Load the file
    int result = read_file_data_to_address(start_cluster, load_address, file_size);
    
    // Free the directory entry
    free(entry);
    
    return result;
}

static int fat32_get_file_size_unlocked(const char* filename, uint32_t* size) {
    if (!filename || !size) return -1;
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (!entry || (entry->attr & ATTR_DIRECTORY)) {
        if (entry) free(entry);
        return -1;
    }
    *size = entry->file_size;
    free(entry);
    return 0;
}

static int fat32_load_file_sized_unlocked(const char* filename,
                                           void* load_address,
                                           uint32_t capacity) {
    if (!filename || (!load_address && capacity != 0)) return -1;
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (!entry || (entry->attr & ATTR_DIRECTORY)) {
        if (entry) free(entry);
        return -1;
    }
    uint32_t size = entry->file_size;
    uint32_t cluster = read_start_cluster(entry);
    free(entry);
    if (size > capacity || size > 0x7FFFFFFFu) return -1;
    if (size == 0) return 0;
    if (!is_valid_cluster(&boot_sector, cluster)) return -1;
    uint32_t read = read_file_data_at(cluster, 0, (char*)load_address,
                                      capacity, size);
    return read == size ? (int)read : -1;
}

// void openAndLoadFile(const char* filename) {
//     struct fat32_dir_entry* entry = find_file_in_directory(filename);
//     if (entry == NULL) {
//         printf("File not found.\n");
//         return;
//     }
//     unsigned int start_cluster = read_start_cluster(entry);
//     int file_size = entry->file_size;
//     char* buffer = malloc(file_size);
//     if (buffer == NULL) {
//         printf("Not enough memory.\n");
//         return;
//     }
//     read_file_data(start_cluster, buffer, sizeof(buffer), file_size);

//     // Process the file data in bufferb
//     // for(int i = 0; i < file_size; i++){
//     //     printf("%c", buffer[i]);
//     // }
//     free(entry); // Free the memory after use
//     free(buffer); // Free the memory after use
// }

typedef struct {
    char name[MAX_PATH_LENGTH];
    uint16_t units[FAT32_MAX_LFN_ENTRIES * FAT32_LFN_CHARS_PER_ENTRY];
    uint8_t checksum;
    uint8_t expected_order;
    uint8_t slot_count;
    bool active;
} fat32_lfn_reader_t;

static void fat32_lfn_reset(fat32_lfn_reader_t* reader) {
    memset(reader, 0, sizeof(*reader));
}

static bool fat32_names_equal(const char* left, const char* right) {
    return reist_unicode_caseless_nfc_equal(left, right) != 0;
}

static uint16_t fat32_lfn_character(const struct fat32_lfn_entry* entry,
                                    uint32_t index) {
    if (index < 5) return entry->name1[index];
    if (index < 11) return entry->name2[index - 5];
    return entry->name3[index - 11];
}

static void fat32_lfn_consume(fat32_lfn_reader_t* reader,
                              const struct fat32_lfn_entry* entry) {
    uint8_t order = entry->order & 0x1FU;
    bool starts = (entry->order & 0x40U) != 0;
    if (entry->attr != ATTR_LONG_NAME || entry->type != 0 ||
        entry->first_cluster_low != 0 || order == 0 ||
        order > FAT32_MAX_LFN_ENTRIES) {
        fat32_lfn_reset(reader);
        return;
    }
    if (starts) {
        fat32_lfn_reset(reader);
        reader->active = true;
        reader->checksum = entry->checksum;
        reader->expected_order = order;
        reader->slot_count = order;
        for (uint32_t i = 0U;
             i < FAT32_MAX_LFN_ENTRIES * FAT32_LFN_CHARS_PER_ENTRY; ++i)
            reader->units[i] = 0xFFFFU;
    }
    if (!reader->active || reader->checksum != entry->checksum ||
        reader->expected_order != order) {
        fat32_lfn_reset(reader);
        return;
    }
    uint32_t base = ((uint32_t)order - 1U) * FAT32_LFN_CHARS_PER_ENTRY;
    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        uint16_t value = fat32_lfn_character(entry, i);
        uint32_t position = base + i;
        if (position >= FAT32_MAX_LFN_ENTRIES *
                        FAT32_LFN_CHARS_PER_ENTRY) {
            fat32_lfn_reset(reader);
            return;
        }
        reader->units[position] = value;
    }
    reader->expected_order--;
}

static bool fat32_lfn_finish(fat32_lfn_reader_t* reader) {
    if (!reader || !reader->active || reader->expected_order != 0U ||
        reader->slot_count == 0U ||
        reader->slot_count > FAT32_MAX_LFN_ENTRIES) return false;
    size_t available = (size_t)reader->slot_count *
                       FAT32_LFN_CHARS_PER_ENTRY;
    size_t unit_count = available;
    bool terminated = false;
    for (size_t i = 0U; i < available; ++i) {
        uint16_t value = reader->units[i];
        if (!terminated && value == 0x0000U) {
            unit_count = i;
            terminated = true;
        } else if ((!terminated && value == 0xFFFFU) ||
                   (terminated && value != 0xFFFFU)) {
            return false;
        }
    }
    if (unit_count == 0U || unit_count > FAT32_MAX_LFN_CHARS) return false;
    size_t output_bytes = 0U;
    return reist_utf16_to_utf8(reader->units, unit_count, reader->name,
                               sizeof(reader->name), &output_bytes) &&
           output_bytes != 0U && fat32_is_valid_name(reader->name);
}

static fat32_lookup_result_t fat32_scan_directory(
        unsigned int dir_cluster, const char* filename, uint32_t wanted_index,
        bool by_index, struct fat32_dir_entry* found,
        char resolved_name[MAX_PATH_LENGTH]) {
    if ((!by_index && !filename) ||
        !is_valid_cluster(&boot_sector, dir_cluster)) {
        return FAT32_LOOKUP_ERROR;
    }

    struct fat32_dir_entry sector_entries[SECTOR_SIZE / sizeof(struct fat32_dir_entry)];
    uint32_t current_cluster = dir_cluster;
    uint32_t traversed = 0;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    uint32_t visible_index = 0;
    fat32_lfn_reader_t lfn;
    fat32_lfn_reset(&lfn);

    while (is_valid_cluster(&boot_sector, current_cluster) &&
           traversed++ < cluster_limit) {
        uint32_t sector = cluster_to_sector(&boot_sector, current_cluster);
        if (sector == INVALID_CLUSTER) {
            return FAT32_LOOKUP_ERROR;
        }

        for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
            if (!ata_read_sector(ata_base_address, sector + i,
                                 sector_entries, ata_is_master)) {
                return FAT32_LOOKUP_ERROR;
            }

            for (uint32_t j = 0;
                 j < SECTOR_SIZE / sizeof(struct fat32_dir_entry); j++) {
                struct fat32_dir_entry* candidate = &sector_entries[j];
                if (candidate->name[0] == 0x00) {
                    return FAT32_LOOKUP_NOT_FOUND;
                }
                if (candidate->name[0] == 0xE5) {
                    fat32_lfn_reset(&lfn);
                    continue;
                }
                if (candidate->attr == ATTR_LONG_NAME) {
                    fat32_lfn_consume(&lfn,
                        (const struct fat32_lfn_entry*)candidate);
                    continue;
                }
                if (candidate->attr & 0x08) {
                    fat32_lfn_reset(&lfn);
                    continue;
                }

                char short_name[13];
                char visible_name[MAX_PATH_LENGTH];
                fat32_format_short_name(candidate, short_name);
                strcpy(visible_name, short_name);
                if (lfn.active &&
                    lfn.checksum == fat32_short_name_checksum(candidate->name) &&
                    fat32_lfn_finish(&lfn)) {
                    strcpy(visible_name, lfn.name);
                }
                bool match = by_index ? visible_index == wanted_index :
                    (fat32_names_equal(visible_name, filename) ||
                     fat32_names_equal(short_name, filename));
                visible_index++;
                fat32_lfn_reset(&lfn);
                if (match) {
                    if (found) *found = *candidate;
                    if (resolved_name) strcpy(resolved_name, visible_name);
                    return FAT32_LOOKUP_FOUND;
                }
            }
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, current_cluster);
        if (next == INVALID_CLUSTER) return FAT32_LOOKUP_ERROR;
        if (is_end_of_cluster_chain(next)) return FAT32_LOOKUP_NOT_FOUND;
        current_cluster = next;
    }

    /* Falling out of the guarded traversal means the directory chain is
     * invalid or cyclic.  It must not be treated as a safe insertion point. */
    return FAT32_LOOKUP_ERROR;
}

fat32_lookup_result_t fat32_lookup_entry_named(
        unsigned int dir_cluster, const char* filename,
        struct fat32_dir_entry* found,
        char resolved_name[MAX_PATH_LENGTH]) {
    return fat32_scan_directory(dir_cluster, filename, 0, false, found,
                                resolved_name);
}

fat32_lookup_result_t fat32_get_directory_entry(
        unsigned int dir_cluster, uint32_t visible_index,
        struct fat32_dir_entry* found,
        char resolved_name[MAX_PATH_LENGTH]) {
    return fat32_scan_directory(dir_cluster, NULL, visible_index, true, found,
                                resolved_name);
}

fat32_lookup_result_t fat32_lookup_entry_in_directory(
        unsigned int dir_cluster, const char* filename,
        struct fat32_dir_entry* found) {
    return fat32_lookup_entry_named(dir_cluster, filename, found, NULL);
}

struct fat32_dir_entry* find_file_in_directory_cluster(unsigned int dir_cluster,
                                                        const char* filename) {
    struct fat32_dir_entry entry;
    if (fat32_lookup_entry_in_directory(dir_cluster, filename, &entry) !=
        FAT32_LOOKUP_FOUND) {
        return NULL;
    }
    struct fat32_dir_entry* found = malloc(sizeof(*found));
    if (!found) return NULL;
    *found = entry;
    return found;
}

// Function to find a file in the current directory
struct fat32_dir_entry* find_file_in_directory(const char* filename) {
    return find_file_in_directory_cluster(current_directory_cluster, filename);
}

static bool clear_data_cluster(uint32_t cluster) {
    uint8_t zero_sector[SECTOR_SIZE];
    memset(zero_sector, 0, sizeof(zero_sector));
    uint32_t first_sector = cluster_to_sector(&boot_sector, cluster);
    if (first_sector == INVALID_CLUSTER) {
        return false;
    }

    for (uint32_t i = 0; i < boot_sector.sectors_per_cluster; i++) {
        if (!fat32_write_sector(first_sector + i, zero_sector)) {
            return false;
        }
    }
    return true;
}

// Advance forward through the cluster chain from a known position, allocating
// and linking new clusters as needed.  `from_cluster` must be the valid cluster
// at `from_index`; the walk only moves forward, so a sequential caller can
// resume from its last position instead of re-walking from the chain start
// (which made a bulk write quadratic in the file length).
static uint32_t advance_file_cluster(uint32_t from_cluster,
                                     uint32_t from_index,
                                     uint32_t target_index,
                                     bool* chain_reclaim_safe) {
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    if (!is_valid_cluster(&boot_sector, from_cluster) ||
        from_index > target_index || target_index >= cluster_limit) {
        return INVALID_CLUSTER;
    }

    uint32_t current = from_cluster;
    for (uint32_t i = from_index; i < target_index; i++) {
        uint32_t next = get_next_cluster_in_chain(&boot_sector, current);
        if (next == INVALID_CLUSTER) {
            return INVALID_CLUSTER;
        }
        if (is_end_of_cluster_chain(next)) {
            uint32_t allocated = allocate_new_cluster(&boot_sector);
            if (allocated == INVALID_CLUSTER || !clear_data_cluster(allocated)) {
                if (allocated != INVALID_CLUSTER) {
                    mark_cluster_in_fat(&boot_sector, allocated, 0);
                }
                return INVALID_CLUSTER;
            }
            if (!mark_cluster_in_fat(&boot_sector, current, allocated)) {
                uint32_t observed =
                    get_next_cluster_in_chain(&boot_sector, current);
                if (observed == allocated) {
                    next = allocated;
                } else {
                    /* Only reclaim after confirming that no directory/file
                     * chain points at the candidate. */
                    if (is_end_of_cluster_chain(observed)) {
                        (void)mark_cluster_in_fat(&boot_sector, allocated, 0);
                    } else {
                        if (chain_reclaim_safe) *chain_reclaim_safe = false;
                        printf("Warning: preserving ambiguous FAT32 link candidate %u\n",
                               allocated);
                    }
                    return INVALID_CLUSTER;
                }
            } else {
                next = allocated;
            }
        }
        if (!is_valid_cluster(&boot_sector, next)) {
            return INVALID_CLUSTER;
        }
        current = next;
    }
    return current;
}

static uint32_t ensure_file_cluster(uint32_t* start_cluster,
                                    uint32_t cluster_index,
                                    bool* chain_reclaim_safe) {
    if (!start_cluster ||
        cluster_index >= get_total_clusters(&boot_sector)) {
        return INVALID_CLUSTER;
    }

    if (!is_valid_cluster(&boot_sector, *start_cluster)) {
        uint32_t first = allocate_new_cluster(&boot_sector);
        if (first == INVALID_CLUSTER || !clear_data_cluster(first)) {
            if (first != INVALID_CLUSTER) {
                mark_cluster_in_fat(&boot_sector, first, 0);
            }
            return INVALID_CLUSTER;
        }
        *start_cluster = first;
    }

    return advance_file_cluster(*start_cluster, 0, cluster_index,
                                chain_reclaim_safe);
}

static void fat32_write_cursor_invalidate(fat32_write_cursor_t* cursor) {
    if (!cursor) return;
    cursor->chain_start = INVALID_CLUSTER;
    cursor->cluster = INVALID_CLUSTER;
    cursor->cluster_index = 0U;
    cursor->next_offset = 0U;
    cursor->valid = false;
}

static bool fat32_write_cursor_matches(const fat32_write_cursor_t* cursor,
                                       uint32_t chain_start,
                                       uint32_t offset,
                                       uint32_t cluster_size) {
    if (!cursor || !cursor->valid || cluster_size == 0U ||
        cursor->chain_start != chain_start || cursor->next_offset != offset ||
        !is_valid_cluster(&boot_sector, cursor->cluster)) {
        return false;
    }

    uint32_t target_index = offset / cluster_size;
    uint32_t cluster_limit = get_total_clusters(&boot_sector);
    return cursor->cluster_index < cluster_limit &&
           target_index >= cursor->cluster_index &&
           target_index - cursor->cluster_index <= 1U;
}

static int fat32_write_failure(fat32_write_cursor_t* cursor,
                               uint32_t written) {
    fat32_write_cursor_invalidate(cursor);
    return written != 0U ? (int)written : -1;
}

int write_file_data_at_checked_cursor(unsigned int* start_cluster,
                                      unsigned int offset,
                                      const void* buffer,
                                      unsigned int bytes_to_write,
                                      bool* chain_reclaim_safe,
                                      fat32_write_cursor_t* cursor) {
    if (!start_cluster || (!buffer && bytes_to_write != 0) ||
        boot_sector.sectors_per_cluster == 0 || bytes_to_write > INT_MAX) {
        fat32_write_cursor_invalidate(cursor);
        return -1;
    }
    if (bytes_to_write == 0) {
        return 0;
    }
    if (offset > UINT32_MAX - bytes_to_write) {
        fat32_write_cursor_invalidate(cursor);
        return -1;
    }

    uint32_t cluster_size = SECTOR_SIZE * boot_sector.sectors_per_cluster;
    uint32_t written = 0;
    uint8_t sector_buffer[SECTOR_SIZE];
    uint8_t verify_buffer[SECTOR_SIZE];
    uint32_t cursor_cluster = INVALID_CLUSTER;
    uint32_t cursor_index = 0;
    bool cursor_valid = fat32_write_cursor_matches(
        cursor, *start_cluster, offset, cluster_size);
    if (cursor_valid) {
        cursor_cluster = cursor->cluster;
        cursor_index = cursor->cluster_index;
    } else {
        fat32_write_cursor_invalidate(cursor);
    }

    while (written < bytes_to_write) {
        uint32_t file_offset = offset + written;
        uint32_t cluster_index = file_offset / cluster_size;
        uint32_t offset_in_cluster = file_offset % cluster_size;
        /* Resume the chain walk from the previously reached cluster whenever
         * the target is at or ahead of it; only the first sector of a bulk
         * write pays the walk from the chain start. */
        uint32_t cluster;
        if (cursor_valid && cluster_index >= cursor_index) {
            cluster = advance_file_cluster(cursor_cluster, cursor_index,
                                           cluster_index, chain_reclaim_safe);
        } else {
            cluster = ensure_file_cluster(start_cluster, cluster_index,
                                          chain_reclaim_safe);
        }
        if (cluster == INVALID_CLUSTER) {
            return fat32_write_failure(cursor, written);
        }
        cursor_cluster = cluster;
        cursor_index = cluster_index;
        cursor_valid = true;

        uint32_t sector_index = offset_in_cluster / SECTOR_SIZE;
        uint32_t sector_offset = offset_in_cluster % SECTOR_SIZE;
        uint32_t first_sector = cluster_to_sector(&boot_sector, cluster);
        if (first_sector == INVALID_CLUSTER) {
            return fat32_write_failure(cursor, written);
        }
        uint32_t sector = first_sector + sector_index;
        uint32_t amount = bytes_to_write - written;
        if (amount > SECTOR_SIZE - sector_offset) {
            amount = SECTOR_SIZE - sector_offset;
        }

        if (sector_offset != 0 || amount != SECTOR_SIZE) {
            if (!ata_read_sector(ata_base_address, sector, sector_buffer,
                                 ata_is_master)) {
                return fat32_write_failure(cursor, written);
            }
        }
        memcpy(sector_buffer + sector_offset,
               (const uint8_t*)buffer + written, amount);
        if (!fat32_write_sector(sector, sector_buffer) ||
            !ata_read_sector(ata_base_address, sector, verify_buffer,
                             ata_is_master) ||
            memcmp(sector_buffer, verify_buffer, SECTOR_SIZE) != 0) {
            return fat32_write_failure(cursor, written);
        }
        written += amount;
    }

    if (cursor) {
        cursor->chain_start = *start_cluster;
        cursor->cluster = cursor_cluster;
        cursor->cluster_index = cursor_index;
        cursor->next_offset = offset + written;
        cursor->valid = true;
    }
    return (int)written;
}

int write_file_data_at_checked(unsigned int* start_cluster,
                               unsigned int offset, const void* buffer,
                               unsigned int bytes_to_write,
                               bool* chain_reclaim_safe) {
    return write_file_data_at_checked_cursor(
        start_cluster, offset, buffer, bytes_to_write, chain_reclaim_safe,
        NULL);
}

int write_file_data_at(unsigned int* start_cluster, unsigned int offset,
                       const void* buffer, unsigned int bytes_to_write) {
    bool chain_reclaim_safe = true;
    return write_file_data_at_checked(start_cluster, offset, buffer,
                                      bytes_to_write, &chain_reclaim_safe);
}

bool update_directory_entry(unsigned int parent_cluster,
                            const unsigned char name[11],
                            const struct fat32_dir_entry* updated_entry) {
    if (!name || !updated_entry ||
        !is_valid_cluster(&boot_sector, parent_cluster)) {
        return false;
    }

    struct fat32_dir_entry entries[SECTOR_SIZE / sizeof(struct fat32_dir_entry)];
    struct fat32_dir_entry verify[SECTOR_SIZE / sizeof(struct fat32_dir_entry)];
    uint32_t current = parent_cluster;
    uint32_t traversed = 0;
    uint32_t limit = get_total_clusters(&boot_sector);
    while (is_valid_cluster(&boot_sector, current) && traversed++ < limit) {
        uint32_t first_sector = cluster_to_sector(&boot_sector, current);
        if (first_sector == INVALID_CLUSTER) return false;
        for (uint32_t sector_index = 0;
             sector_index < boot_sector.sectors_per_cluster; sector_index++) {
            uint32_t sector = first_sector + sector_index;
            if (!ata_read_sector(ata_base_address, sector, entries,
                                 ata_is_master)) {
                return false;
            }
            for (uint32_t i = 0;
                 i < SECTOR_SIZE / sizeof(struct fat32_dir_entry); i++) {
                if (entries[i].name[0] == 0x00) {
                    return false;
                }
                if (entries[i].name[0] != 0xE5 &&
                    (entries[i].attr & 0x08u) == 0 &&
                    (entries[i].attr & 0x0Fu) != 0x0Fu &&
                    memcmp(entries[i].name, name, 11) == 0) {
                    entries[i] = *updated_entry;
                    if (!fat32_write_sector(sector, entries) ||
                        !ata_read_sector(ata_base_address, sector, verify,
                                         ata_is_master) ||
                        memcmp(entries, verify, SECTOR_SIZE) != 0) {
                        return false;
                    }
                    return true;
                }
            }
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, current);
        if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next)) {
            break;
        }
        current = next;
    }
    return false;
}

static bool fat32_create_file_unlocked(const char* filename) {
    if (!fat32_is_valid_name(filename) ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }
    if (!fat32_prepare_write()) return false;

    struct fat32_dir_entry existing;
    if (fat32_lookup_entry_in_directory(current_directory_cluster, filename,
                                        &existing) != FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }

    // Empty FAT files conventionally have start cluster 0.  Allocate their
    // first cluster only when data is actually written.
    if (!add_entry_to_directory(&boot_sector, current_directory_cluster,
                                filename, 0, 0x20)) {
        struct fat32_dir_entry observed;
        if (fat32_lookup_entry_in_directory(current_directory_cluster,
                                            filename, &observed) !=
                FAT32_LOOKUP_FOUND ||
            read_start_cluster(&observed) != 0 || observed.file_size != 0 ||
            observed.attr != ATTR_ARCHIVE) {
            printf("Failed to add a directory entry for the new file.\n");
            return false;
        }
    }

    return true;
}

static bool fat32_delete_file_unlocked(const char* filename) {
    if (!filename || !fat32_is_valid_name(filename) ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }
    if (!fat32_prepare_write()) return false;
    // 1. Find the directory entry for the file to delete
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL) {
        printf("File not found.\n");
        return false;
    }

    if (entry->attr & (ATTR_DIRECTORY | ATTR_READ_ONLY)) {
        free(entry);
        return false;
    }
    
    // Save cluster info before freeing entry
    unsigned int start_cluster = read_start_cluster(entry);
    
    // Remove the name first.  If freeing later fails, the result is a leaked
    // chain rather than a visible directory entry pointing at reusable data.
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster,
                                     entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        free(entry);
        return false;
    }

    if (start_cluster >= 2 &&
        !free_cluster_chain(&boot_sector, start_cluster)) {
        printf("Failed to free the file's cluster chain.\n");
        free(entry);
        return false;
    }
    
    free(entry);  // Free allocated memory
    
    // Sync FSInfo after file deletion
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

// Function to open a file and return a pointer to the file data
static FILE* fat32_open_file_unlocked(const char* filename, const char* mode) {
    if (!filename || !mode ||
        (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') ||
        mode[1] != '\0') {
        return NULL;
    }
    if (mode[0] != 'r' && !fat32_prepare_write()) return NULL;
    struct fat32_dir_entry* entry = find_file_in_directory(filename);
    if (entry == NULL || (entry->attr & ATTR_DIRECTORY) ||
        (mode[0] != 'r' && (entry->attr & ATTR_READ_ONLY))) {
        printf("File not found.\n");
        if (entry) free(entry);
        return NULL;
    }

    unsigned int start_cluster = read_start_cluster(entry);
    uint32_t file_size = entry->file_size;

    FILE* file = (FILE*)malloc(sizeof(FILE));
    if (file == NULL) {
        printf("Not enough memory.\n");
        free(entry);
        return NULL;
    }

    if (mode[0] == 'w') {
        struct fat32_dir_entry truncated = *entry;
        truncated.first_cluster_high = 0;
        truncated.first_cluster_low = 0;
        truncated.file_size = 0;
        set_fat32_time(&truncated.write_time, &truncated.write_date);
        if (!update_directory_entry(current_directory_cluster,
                                    entry->name, &truncated)) {
            /* The sector write may have succeeded even if its verification
             * read failed.  Re-read before deciding whether the old chain is
             * still owned by the directory entry. */
            struct fat32_dir_entry observed;
            fat32_lookup_result_t observed_result =
                fat32_lookup_entry_in_directory(current_directory_cluster,
                                                filename, &observed);
            if (observed_result != FAT32_LOOKUP_FOUND ||
                memcmp(&observed, &truncated, sizeof(truncated)) != 0) {
                free(entry);
                free(file);
                return NULL;
            }
        }
        if (is_valid_cluster(&boot_sector, start_cluster) &&
            !free_cluster_chain(&boot_sector, start_cluster)) {
            printf("Warning: FAT32 truncation left an unreachable cluster chain\n");
        }
        *entry = truncated;
        start_cluster = 0;
        file_size = 0;
        (void)write_fsinfo();
    }

    file->position = mode[0] == 'a' ? file_size : 0;
    file->size = file_size;
    file->base = NULL;
    file->ptr = NULL;
    strncpy(file->mode, mode, sizeof(file->mode) - 1);
    file->mode[sizeof(file->mode) - 1] = '\0';
    strncpy(file->name, filename, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->parent_cluster = current_directory_cluster;
    file->partition_lba = partition_lba_offset;
    file->device_base = ata_base_address;
    file->device_master = ata_is_master ? 1u : 0u;
    file->start_cluster = start_cluster;

    free(entry);

    return file;
}

static bool fat32_activate_file_volume(const FILE* file) {
    if (!file) return false;
    if (ata_base_address == file->device_base &&
        ata_is_master == (file->device_master != 0) &&
        partition_lba_offset == file->partition_lba) {
        return true;
    }
    return fat32_init_fs_at(file->device_base, file->device_master != 0,
                            file->partition_lba) == SUCCESS;
}

typedef struct {
    struct fat32_boot_sector boot;
    struct fat32_fsinfo info;
    bool info_valid;
    unsigned int directory_cluster;
    unsigned short base;
    bool master;
    unsigned int partition;
    bool write_supported;
    drive_t* drive;
} fat32_saved_context_t;

static bool fat32_file_volume_differs(const FILE* file) {
    return file && (ata_base_address != file->device_base ||
        ata_is_master != (file->device_master != 0) ||
        partition_lba_offset != file->partition_lba);
}

static void fat32_save_global_context(fat32_saved_context_t* saved) {
    saved->boot = boot_sector;
    saved->info = fsinfo;
    saved->info_valid = fsinfo_valid;
    saved->directory_cluster = current_directory_cluster;
    saved->base = ata_base_address;
    saved->master = ata_is_master;
    saved->partition = partition_lba_offset;
    saved->write_supported = fat32_write_supported;
    saved->drive = current_drive;
}

static void fat32_restore_global_context(const fat32_saved_context_t* saved) {
    boot_sector = saved->boot;
    fsinfo = saved->info;
    fsinfo_valid = saved->info_valid;
    current_directory_cluster = saved->directory_cluster;
    ata_base_address = saved->base;
    ata_is_master = saved->master;
    partition_lba_offset = saved->partition;
    fat32_write_supported = saved->write_supported;
    current_drive = saved->drive;
}

// read file
static int fat32_read_file_unlocked(FILE* file, void* buffer,
                                    unsigned int buffer_size,
                                    unsigned int bytes_to_read) {
    if (!file || !buffer || file->mode[0] == 'w') {
        printf("Error: File is not open for reading.\n");
        return 0;
    }
    if (!fat32_activate_file_volume(file)) return 0;

    struct fat32_dir_entry current;
    if (fat32_lookup_entry_in_directory(file->parent_cluster,
                                        file->name, &current) !=
            FAT32_LOOKUP_FOUND ||
        (current.attr & ATTR_DIRECTORY)) {
        return 0;
    }
    file->start_cluster = read_start_cluster(&current);
    file->size = current.file_size;

    if (file->position >= file->size) {
        return 0;
    }
    if (bytes_to_read > file->size - file->position) {
        bytes_to_read = file->size - file->position;
    }
    if (bytes_to_read > INT_MAX) bytes_to_read = INT_MAX;

    unsigned int result = read_file_data_at(file->start_cluster,
                                            (unsigned int)file->position,
                                            (char*)buffer, buffer_size,
                                            bytes_to_read);
    file->position += result;
    return (int)result;
}

static int fat32_write_file_unlocked(FILE* file, const void* buffer,
                                     unsigned int buffer_size,
                                     unsigned int bytes_to_write) {
    if (!file || !buffer || file->mode[0] == 'r') {
        return -1;
    }
    if (!fat32_activate_file_volume(file)) return -1;
    if (!fat32_prepare_write()) return -1;
    if (bytes_to_write > INT_MAX) return -1;
    if (bytes_to_write > buffer_size) {
        bytes_to_write = buffer_size;
    }
    if (file->position > UINT32_MAX - bytes_to_write) {
        return -1;
    }

    struct fat32_dir_entry current;
    if (fat32_lookup_entry_in_directory(file->parent_cluster, file->name,
                                        &current) != FAT32_LOOKUP_FOUND ||
        (current.attr & (ATTR_DIRECTORY | ATTR_READ_ONLY))) {
        return -1;
    }
    struct fat32_dir_entry* entry = &current;
    const struct fat32_dir_entry original = current;

    uint32_t original_start_cluster = read_start_cluster(entry);
    uint32_t start_cluster = original_start_cluster;
    uint32_t original_tail = INVALID_CLUSTER;
    if (is_valid_cluster(&boot_sector, original_start_cluster) &&
        !fat32_get_chain_tail(&boot_sector, original_start_cluster,
                              &original_tail)) {
        return -1;
    }
    bool chain_reclaim_safe = true;
    file->start_cluster = start_cluster;
    file->size = entry->file_size;
    if (file->mode[0] == 'a') file->position = file->size;
    uint8_t zeroes[SECTOR_SIZE];
    memset(zeroes, 0, sizeof(zeroes));
    uint32_t gap_pos = (uint32_t)file->size;
    while (gap_pos < file->position) {
        uint32_t amount = (uint32_t)file->position - gap_pos;
        if (amount > sizeof(zeroes)) amount = sizeof(zeroes);
        int zeroed = write_file_data_at_checked(&start_cluster, gap_pos,
                                                zeroes, amount,
                                                &chain_reclaim_safe);
        if (zeroed != (int)amount) {
            if (chain_reclaim_safe) {
                if (is_valid_cluster(&boot_sector, original_start_cluster)) {
                    (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                     original_tail);
                } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                    (void)free_cluster_chain(&boot_sector, start_cluster);
                }
                (void)write_fsinfo();
            }
            return -1;
        }
        gap_pos += amount;
    }

    int written = write_file_data_at_checked(
        &start_cluster, (uint32_t)file->position, buffer, bytes_to_write,
        &chain_reclaim_safe);
    if (written < 0 || !chain_reclaim_safe) {
        if (chain_reclaim_safe) {
            if (is_valid_cluster(&boot_sector, original_start_cluster)) {
                (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                 original_tail);
            } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                (void)free_cluster_chain(&boot_sector, start_cluster);
            }
            (void)write_fsinfo();
        }
        return -1;
    }

    entry->first_cluster_high = (uint16_t)(start_cluster >> 16);
    entry->first_cluster_low = (uint16_t)start_cluster;
    uint32_t end = (uint32_t)file->position + (uint32_t)written;
    if (end > entry->file_size) {
        entry->file_size = end;
    }
    set_fat32_time(&entry->write_time, &entry->write_date);
    bool metadata_ok = update_directory_entry(file->parent_cluster,
                                              entry->name, entry);
    if (!metadata_ok) {
        struct fat32_dir_entry observed;
        fat32_lookup_result_t observed_result =
            fat32_lookup_entry_in_directory(file->parent_cluster, file->name,
                                            &observed);
        if (observed_result == FAT32_LOOKUP_FOUND &&
            memcmp(&observed, entry, sizeof(*entry)) == 0) {
            metadata_ok = true;
        } else {
            if (observed_result == FAT32_LOOKUP_FOUND &&
                memcmp(&observed, &original, sizeof(original)) == 0 &&
                chain_reclaim_safe) {
                if (is_valid_cluster(&boot_sector, original_start_cluster)) {
                    (void)fat32_reclaim_chain_suffix(&boot_sector,
                                                     original_tail);
                } else if (is_valid_cluster(&boot_sector, start_cluster)) {
                    (void)free_cluster_chain(&boot_sector, start_cluster);
                }
                (void)write_fsinfo();
            }
            return -1;
        }
    }
    file->start_cluster = start_cluster;
    file->position = end;
    file->size = entry->file_size;
    (void)write_fsinfo();
    return written;
}

static bool fat32_replace_file_unlocked(const char* filename,
                                        const void* buffer, uint32_t size) {
    if (!filename || (size > 0 && !buffer) ||
        size > 0x7FFFFFFFu ||
        !fat32_is_valid_name(filename) ||
        strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        return false;
    }
    if (!fat32_prepare_write()) return false;

    struct fat32_dir_entry previous;
    fat32_lookup_result_t lookup = fat32_lookup_entry_in_directory(
        current_directory_cluster, filename, &previous);
    if (lookup == FAT32_LOOKUP_ERROR) return false;

    bool created_target = false;
    if (lookup == FAT32_LOOKUP_NOT_FOUND) {
        bool create_reported_success = add_entry_to_directory(
            &boot_sector, current_directory_cluster, filename, 0,
            ATTR_ARCHIVE);
        if (create_reported_success) {
            lookup = FAT32_LOOKUP_ERROR;
            for (unsigned int attempt = 0; attempt < 3 &&
                 lookup == FAT32_LOOKUP_ERROR; ++attempt) {
                lookup = fat32_lookup_entry_in_directory(
                    current_directory_cluster, filename, &previous);
            }
            if (lookup != FAT32_LOOKUP_FOUND ||
                read_start_cluster(&previous) != 0 ||
                previous.file_size != 0 || previous.attr != ATTR_ARCHIVE) {
                return false;
            }
        } else {
            lookup = fat32_lookup_entry_in_directory(
                current_directory_cluster, filename, &previous);
            if (lookup != FAT32_LOOKUP_FOUND ||
                read_start_cluster(&previous) != 0 ||
                previous.file_size != 0 || previous.attr != ATTR_ARCHIVE) {
                return false;
            }
        }
        created_target = true;
    }
    if (previous.attr & (ATTR_DIRECTORY | ATTR_READ_ONLY)) return false;

    uint32_t previous_cluster = read_start_cluster(&previous);
    uint32_t new_cluster = 0;
    bool chain_reclaim_safe = true;
    int written = size == 0 ? 0 :
        write_file_data_at_checked(&new_cluster, 0, buffer, size,
                                   &chain_reclaim_safe);
    if (written < 0 || (uint32_t)written != size) {
        if (chain_reclaim_safe &&
            is_valid_cluster(&boot_sector, new_cluster) &&
            !free_cluster_chain(&boot_sector, new_cluster)) {
            printf("Warning: FAT32 failed to reclaim an incomplete replacement\n");
        }
        if (created_target) (void)fat32_delete_file(filename);
        (void)write_fsinfo();
        return false;
    }

    struct fat32_dir_entry committed = previous;
    committed.first_cluster_high = (uint16_t)(new_cluster >> 16);
    committed.first_cluster_low = (uint16_t)new_cluster;
    committed.file_size = size;
    set_fat32_time(&committed.write_time, &committed.write_date);

    bool committed_ok = update_directory_entry(current_directory_cluster,
                                               previous.name, &committed);
    if (!committed_ok) {
        /* A failed verify read is ambiguous: the sector write may still have
         * committed.  Resolve ownership by reading the entry again before
         * deciding whether the new chain can be reclaimed. */
        struct fat32_dir_entry observed;
        fat32_lookup_result_t observed_result =
            fat32_lookup_entry_in_directory(current_directory_cluster,
                                            filename, &observed);
        if (observed_result == FAT32_LOOKUP_FOUND &&
            memcmp(&observed, &committed, sizeof(committed)) == 0) {
            committed_ok = true;
        } else if (observed_result == FAT32_LOOKUP_FOUND &&
                   memcmp(&observed, &previous, sizeof(previous)) == 0) {
            if (chain_reclaim_safe &&
                is_valid_cluster(&boot_sector, new_cluster) &&
                !free_cluster_chain(&boot_sector, new_cluster)) {
                printf("Warning: FAT32 failed to reclaim a rejected replacement\n");
            }
            if (created_target) (void)fat32_delete_file(filename);
            (void)write_fsinfo();
            return false;
        } else {
            /* Do not free either chain when ownership cannot be established;
             * leaking space is safer than leaving a directory entry dangling. */
            printf("FAT32 replacement status is ambiguous; preserving both chains\n");
            return false;
        }
    }

    if (committed_ok && is_valid_cluster(&boot_sector, previous_cluster) &&
        previous_cluster != new_cluster &&
        !free_cluster_chain(&boot_sector, previous_cluster)) {
        printf("Warning: FAT32 replacement committed but old space was not reclaimed\n");
    }
    (void)write_fsinfo();
    return committed_ok;
}

int fat32_load_file(const char* filename, void* load_address) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_load_file_unlocked(filename, load_address);
    fat32_operation_end(flags);
    return result;
}

int fat32_get_file_size(const char* filename, uint32_t* size) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_get_file_size_unlocked(filename, size);
    fat32_operation_end(flags);
    return result;
}

int fat32_load_file_sized(const char* filename, void* load_address,
                          uint32_t capacity) {
    uint32_t flags = fat32_operation_begin();
    int result = fat32_load_file_sized_unlocked(filename, load_address,
                                                 capacity);
    fat32_operation_end(flags);
    return result;
}

bool fat32_create_file(const char* filename) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_create_file_unlocked(filename);
    fat32_operation_end(flags);
    return result;
}

bool fat32_delete_file(const char* filename) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_delete_file_unlocked(filename);
    fat32_operation_end(flags);
    return result;
}

FILE* fat32_open_file(const char* filename, const char* mode) {
    uint32_t flags = fat32_operation_begin();
    FILE* result = fat32_open_file_unlocked(filename, mode);
    fat32_operation_end(flags);
    return result;
}

int fat32_read_file(FILE* file, void* buffer, unsigned int buffer_size,
                    unsigned int bytes_to_read) {
    uint32_t flags = fat32_operation_begin();
    bool restore_context = fat32_file_volume_differs(file);
    fat32_saved_context_t saved;
    if (restore_context) fat32_save_global_context(&saved);
    int result = fat32_read_file_unlocked(file, buffer, buffer_size,
                                           bytes_to_read);
    if (restore_context && fat32_context_sync_hook) {
        fat32_context_sync_hook();
    }
    if (restore_context) fat32_restore_global_context(&saved);
    fat32_operation_end(flags);
    return result;
}

int fat32_write_file(FILE* file, const void* buffer,
                     unsigned int buffer_size,
                     unsigned int bytes_to_write) {
    uint32_t flags = fat32_operation_begin();
    bool restore_context = fat32_file_volume_differs(file);
    fat32_saved_context_t saved;
    if (restore_context) fat32_save_global_context(&saved);
    int result = fat32_write_file_unlocked(file, buffer, buffer_size,
                                            bytes_to_write);
    if (restore_context && fat32_context_sync_hook) {
        fat32_context_sync_hook();
    }
    if (restore_context) fat32_restore_global_context(&saved);
    fat32_operation_end(flags);
    return result;
}

bool fat32_replace_file(const char* filename, const void* buffer,
                        uint32_t size) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_replace_file_unlocked(filename, buffer, size);
    fat32_operation_end(flags);
    return result;
}
