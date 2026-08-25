/**
 * @file fs/fat32/fat32_dir.c
 * @brief FAT32-Verzeichnisnavigation und Eintragsverwaltung.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: LFN/8.3-Einträge und Verzeichnisketten werden längenbegrenzt validiert.
 */
#include "fat32.h"
#include "include/reist/utf.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "drivers/bus/drives.h"


// Function to read a directory path and return if it exists
static bool fat32_read_dir_unlocked(const char* path) {
    if (!path || strlen(path) >= MAX_PATH_LENGTH) return false;
    unsigned int current_cluster = boot_sector.root_cluster; // Assuming boot_sector is defined and initialized elsewhere
    char temp_path[MAX_PATH_LENGTH]; // Temporary path buffer
    
    // Safe string copy with bounds checking
    strncpy(temp_path, path, MAX_PATH_LENGTH - 1);
    temp_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = temp_path;
    if (start[0] == '/') {
        start++;
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        //printf("Searching for directory: %s\n", token);
        // Find the next directory in the path
        current_cluster = find_next_cluster(&boot_sector, token, current_cluster);
        if (current_cluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Now current_cluster points to the cluster of the target directory
    read_cluster_dir_entries(current_cluster);
    return true;
}

static bool fat32_change_directory_unlocked(const char* path) {
    if (!path || strlen(path) >= MAX_PATH_LENGTH) return false;
    unsigned int target_cluster = current_directory_cluster; // Start from the current directory
    char temp_path[MAX_PATH_LENGTH]; // Temporary path buffer
    
    // Safe string copy with bounds checking
    strncpy(temp_path, path, MAX_PATH_LENGTH - 1);
    temp_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    
    char* token, * saveptr;
    // Check for leading '/' and skip it if present
    char* start = temp_path;
    if (start[0] == '/') {
        start++;
        target_cluster = boot_sector.root_cluster; // Absolute path, start from root
    }
    token = strtok_r(start, "/", &saveptr);
    while (token != NULL) {
        // Find the next directory in the path
        target_cluster = find_next_cluster(&boot_sector, token, target_cluster);
        if (target_cluster == INVALID_CLUSTER) {
            printf("Directory not found: %s\n", token);
            return false;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }
    // Update current_directory_cluster on successful path change
    current_directory_cluster = target_cluster;
    return true;
}

static bool fat32_create_dir_unlocked(const char* dirname) {

    if (!dirname) return false;

    if (!fat32_is_valid_name(dirname) ||
        strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return false;
    }
    if (!fat32_prepare_write()) return false;
    struct fat32_dir_entry existing;
    if (fat32_lookup_entry_in_directory(current_directory_cluster, dirname,
                                        &existing) !=
            FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }
    // 1. Allocate and publish ownership of a new cluster.
    unsigned int new_dir_cluster = allocate_new_cluster(&boot_sector);
    if (new_dir_cluster == INVALID_CLUSTER) {
        printf("Error: Failed to allocate a new cluster for the directory.\n");
        return false;
    }
    
    // 3. Initialize the new directory's cluster
    unsigned int entries_per_cluster = get_entries_per_cluster(&boot_sector);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* dir_entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!dir_entries) {
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);
        return false;
    }
    memset(dir_entries, 0, entries_size);
    initialize_new_directory_entries(dir_entries, new_dir_cluster, current_directory_cluster);

    if (!write_cluster(&boot_sector, new_dir_cluster, dir_entries)) {
        printf("Error: Failed to write the initialized entries to the new cluster.\n");
        // ROLLBACK: Free the allocated cluster
        printf("Rolling back: Freeing allocated cluster %u\n", new_dir_cluster);
        mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);  // Mark as free
        free(dir_entries);
        return false;
    }
    free(dir_entries);
    
    // 4. Update the parent directory
    bool safe_to_reclaim_target = true;
    if (!add_entry_to_directory_checked(
            &boot_sector, current_directory_cluster, dirname,
            new_dir_cluster, ATTR_DIRECTORY, &safe_to_reclaim_target)) {
        printf("Error: Failed to update the parent directory.\n");
        struct fat32_dir_entry observed;
        fat32_lookup_result_t observed_result =
            fat32_lookup_entry_in_directory(current_directory_cluster,
                                            dirname, &observed);
        if (observed_result == FAT32_LOOKUP_FOUND &&
            read_start_cluster(&observed) == new_dir_cluster &&
            (observed.attr & ATTR_DIRECTORY)) {
            /* The parent write committed but its immediate verification was
             * inconclusive. Ownership is established by the re-read. */
        } else if (observed_result == FAT32_LOOKUP_NOT_FOUND &&
                   safe_to_reclaim_target) {
            (void)mark_cluster_in_fat(&boot_sector, new_dir_cluster, 0);
            return false;
        } else {
            printf("Warning: preserving ambiguous directory cluster %u\n",
                   new_dir_cluster);
            return false;
        }
    }
    
    // Sync FSInfo after directory creation
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

void initialize_new_directory_entries(struct fat32_dir_entry* entries, unsigned int new_dir_cluster, unsigned int parent_cluster) {
    // Create '.' entry
    create_directory_entry(&entries[0], ".", new_dir_cluster, ATTR_DIRECTORY);
    // Create '..' entry
    create_directory_entry(&entries[1], "..", parent_cluster, ATTR_DIRECTORY);
    // The rest of the entries are already initialized to zero
}

void create_directory_entry(struct fat32_dir_entry* entry, const char* name, unsigned int cluster, unsigned char attributes) {
    // Clear the entry
    memset(entry, 0, sizeof(struct fat32_dir_entry));
    // Convert and set the name in 8.3 format
    convert_to_83_format(entry->name, name);
    // Set the first cluster number
    entry->first_cluster_high = (cluster >> 16) & 0xFFFF;
    entry->first_cluster_low = cluster & 0xFFFF;

    // Set attributes (e.g., directory attribute)
    entry->attr = attributes;
    
    // Set creation, last access, and last write time using real RTC values
    set_fat32_time(&entry->crt_time, &entry->crt_date);
    set_fat32_time(NULL, &entry->last_access_date); // Date only for last access
    set_fat32_time(&entry->write_time, &entry->write_date);
    
    // Set creation time tenths (0-199 for 0.0-1.9 seconds, we'll use 0 for simplicity)
    entry->crt_time_tenth = 0;
}

typedef struct {
    uint32_t cluster;
    uint32_t index;
} fat32_slot_location_t;

static bool fat32_short_case_representable(const char* name,
                                           uint8_t* nt_case) {
    bool base_lower = false, base_upper = false;
    bool ext_lower = false, ext_upper = false;
    bool extension = false;
    for (const char* cursor = name; *cursor; cursor++) {
        if (*cursor == '.') {
            extension = true;
            continue;
        }
        if (*cursor >= 'a' && *cursor <= 'z') {
            if (extension) ext_lower = true; else base_lower = true;
        } else if (*cursor >= 'A' && *cursor <= 'Z') {
            if (extension) ext_upper = true; else base_upper = true;
        }
    }
    if ((base_lower && base_upper) || (ext_lower && ext_upper)) return false;
    if (nt_case)
        *nt_case = (base_lower ? 0x08U : 0U) | (ext_lower ? 0x10U : 0U);
    return true;
}

static bool fat32_make_unique_alias(uint32_t parent_cluster, const char* name,
                                    char alias[13]) {
    const char* dot = NULL;
    for (const char* cursor = name; *cursor; cursor++)
        if (*cursor == '.') dot = cursor;
    size_t stem_end = dot && dot != name ? (size_t)(dot - name) : strlen(name);
    char stem[9];
    char extension[4];
    size_t stem_used = 0;
    size_t extension_used = 0;
    for (size_t i = 0; i < stem_end && stem_used < 8; i++) {
        unsigned char value = (unsigned char)name[i];
        if (isalnum(value)) stem[stem_used++] = (char)toupper(value);
    }
    if (stem_used == 0) stem[stem_used++] = '_';
    stem[stem_used] = '\0';
    if (dot) {
        for (const char* cursor = dot + 1; *cursor && extension_used < 3;
             cursor++) {
            unsigned char value = (unsigned char)*cursor;
            if (isalnum(value))
                extension[extension_used++] = (char)toupper(value);
        }
    }
    extension[extension_used] = '\0';

    for (uint32_t sequence = 1; sequence <= 999999U; sequence++) {
        char digits[7];
        uint32_t value = sequence;
        size_t digit_count = 0;
        do {
            digits[digit_count++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value && digit_count < sizeof(digits) - 1U);
        size_t prefix = 8U - digit_count - 1U;
        if (prefix > stem_used) prefix = stem_used;
        size_t used = 0;
        for (size_t i = 0; i < prefix; i++) alias[used++] = stem[i];
        alias[used++] = '~';
        while (digit_count) alias[used++] = digits[--digit_count];
        if (extension_used) {
            alias[used++] = '.';
            for (size_t i = 0; i < extension_used; i++)
                alias[used++] = extension[i];
        }
        alias[used] = '\0';
        struct fat32_dir_entry collision;
        fat32_lookup_result_t result = fat32_lookup_entry_in_directory(
            parent_cluster, alias, &collision);
        if (result == FAT32_LOOKUP_NOT_FOUND) return true;
        if (result == FAT32_LOOKUP_ERROR) return false;
    }
    return false;
}

static void fat32_set_lfn_character(struct fat32_lfn_entry* entry,
                                    uint32_t index, uint16_t value) {
    if (index < 5) entry->name1[index] = value;
    else if (index < 11) entry->name2[index - 5] = value;
    else entry->name3[index - 11] = value;
}

static void fat32_build_lfn_slot(struct fat32_lfn_entry* entry,
                                 const uint16_t* name, size_t length,
                                 uint8_t order,
                                 uint8_t total, uint8_t checksum) {
    memset(entry, 0xFF, sizeof(*entry));
    entry->order = order | (order == total ? 0x40U : 0U);
    entry->attr = ATTR_LONG_NAME;
    entry->type = 0;
    entry->checksum = checksum;
    entry->first_cluster_low = 0;
    size_t base = ((size_t)order - 1U) * FAT32_LFN_CHARS_PER_ENTRY;
    for (uint32_t i = 0; i < FAT32_LFN_CHARS_PER_ENTRY; i++) {
        size_t position = base + i;
        uint16_t value = position < length ? name[position] :
                         position == length ? 0x0000U : 0xFFFFU;
        fat32_set_lfn_character(entry, i, value);
    }
}

bool add_entry_to_directory_checked(struct fat32_boot_sector* bs,
                                    unsigned int parent_cluster,
                                    const char* dirname,
                                    unsigned int new_dir_cluster,
                                    unsigned char attributes,
                                    bool* safe_to_reclaim_target) {
    if (safe_to_reclaim_target) *safe_to_reclaim_target = true;
    if (!bs || !fat32_is_valid_name(dirname) ||
        !is_valid_cluster(bs, parent_cluster) ||
        (new_dir_cluster != 0 && !is_valid_cluster(bs, new_dir_cluster))) {
        return false;
    }
    uint16_t lfn_units[FAT32_MAX_LFN_CHARS];
    size_t lfn_unit_count = 0U;
    size_t dirname_bytes = strlen(dirname);
    if (!reist_utf8_to_utf16(dirname, dirname_bytes, lfn_units,
                             FAT32_MAX_LFN_CHARS, &lfn_unit_count) ||
        lfn_unit_count == 0U) return false;

    struct fat32_dir_entry duplicate;
    fat32_lookup_result_t duplicate_result =
        fat32_lookup_entry_in_directory(parent_cluster, dirname, &duplicate);
    if (duplicate_result != FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }

    uint8_t nt_case = 0;
    bool long_name = !fat32_is_valid_short_name(dirname) ||
                     !fat32_short_case_representable(dirname, &nt_case);
    char alias[13];
    if (long_name) {
        if (!fat32_make_unique_alias(parent_cluster, dirname, alias))
            return false;
    } else {
        strcpy(alias, dirname);
    }
    struct fat32_dir_entry new_entry;
    create_directory_entry(&new_entry, alias, new_dir_cluster, attributes);
    if (!long_name) new_entry.nt_res = nt_case;
    uint32_t lfn_count = long_name ?
        ((uint32_t)lfn_unit_count + FAT32_LFN_CHARS_PER_ENTRY - 1U) /
            FAT32_LFN_CHARS_PER_ENTRY : 0U;
    uint32_t needed = lfn_count + 1U;
    fat32_slot_location_t locations[FAT32_MAX_LFN_ENTRIES + 1U];
    uint32_t free_run = 0;
    bool directory_end = false;

    unsigned int entries_per_cluster = get_entries_per_cluster(bs);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!entries) {
        return false;
    }
    unsigned int current_cluster = parent_cluster;
    unsigned int traversed = 0;
    unsigned int cluster_limit = get_total_clusters(bs);

    while (is_valid_cluster(bs, current_cluster) &&
           traversed++ < cluster_limit) {
        if (!read_cluster(bs, current_cluster, entries)) {
            free(entries);
            return false;
        }

        for (unsigned int i = 0; i < entries_per_cluster; ++i) {
            if (entries[i].name[0] == 0) directory_end = true;
            if (directory_end || entries[i].name[0] == 0xE5) {
                locations[free_run].cluster = current_cluster;
                locations[free_run].index = i;
                if (++free_run == needed) goto publish_entry;
            } else {
                free_run = 0;
            }
        }

        unsigned int next = get_next_cluster_in_chain(bs, current_cluster);
        if (next == INVALID_CLUSTER) {
            free(entries);
            return false;
        }
        if (is_end_of_cluster_chain(next)) {
            unsigned int new_cluster = allocate_new_cluster(bs);
            if (new_cluster == INVALID_CLUSTER) {
                free(entries);
                return false;
            }

            memset(entries, 0, entries_size);
            if (!write_cluster(bs, new_cluster, entries)) {
                mark_cluster_in_fat(bs, new_cluster, 0);
                free(entries);
                return false;
            }
            if (!mark_cluster_in_fat(bs, current_cluster, new_cluster)) {
                uint32_t observed =
                    get_next_cluster_in_chain(bs, current_cluster);
                if (observed != new_cluster) {
                    if (is_end_of_cluster_chain(observed)) {
                        (void)mark_cluster_in_fat(bs, new_cluster, 0);
                    } else {
                        if (safe_to_reclaim_target) {
                            *safe_to_reclaim_target = false;
                        }
                        printf("Warning: preserving ambiguous directory extension %u\n",
                               new_cluster);
                    }
                    free(entries);
                    return false;
                }
            }
            current_cluster = new_cluster;
            continue;
        }
        current_cluster = next;
    }

    free(entries);
    return false;

publish_entry:
    {
        struct fat32_dir_entry raw[FAT32_MAX_LFN_ENTRIES + 1U];
        memset(raw, 0, sizeof(raw));
        uint8_t checksum = fat32_short_name_checksum(new_entry.name);
        for (uint32_t i = 0; i < lfn_count; i++) {
            uint8_t order = (uint8_t)(lfn_count - i);
            fat32_build_lfn_slot((struct fat32_lfn_entry*)&raw[i], lfn_units,
                                 lfn_unit_count, order,
                                 (uint8_t)lfn_count, checksum);
        }
        raw[lfn_count] = new_entry;

        uint32_t first = 0;
        while (first < needed) {
            uint32_t target_cluster = locations[first].cluster;
            if (!read_cluster(bs, target_cluster, entries)) {
                if (safe_to_reclaim_target) *safe_to_reclaim_target = false;
                free(entries);
                return false;
            }
            uint32_t cursor = first;
            while (cursor < needed &&
                   locations[cursor].cluster == target_cluster) {
                entries[locations[cursor].index] = raw[cursor];
                cursor++;
            }
            if (!write_cluster(bs, target_cluster, entries)) {
                bool committed = read_cluster(bs, target_cluster, entries);
                for (uint32_t verify = first;
                     committed && verify < cursor; verify++) {
                    committed = memcmp(&entries[locations[verify].index],
                                       &raw[verify], sizeof(raw[verify])) == 0;
                }
                if (!committed) {
                    if (safe_to_reclaim_target)
                        *safe_to_reclaim_target = false;
                    free(entries);
                    return false;
                }
            }
            first = cursor;
        }
        free(entries);
        return true;
    }
}

bool add_entry_to_directory(struct fat32_boot_sector* bs,
                            unsigned int parent_cluster,
                            const char* dirname,
                            unsigned int new_dir_cluster,
                            unsigned char attributes) {
    bool safe_to_reclaim_target = true;
    return add_entry_to_directory_checked(
        bs, parent_cluster, dirname, new_dir_cluster, attributes,
        &safe_to_reclaim_target);
}

bool is_directory_empty(struct fat32_dir_entry* entry) {
    if (!entry || !(entry->attr & ATTR_DIRECTORY)) {
        return false;
    }

    unsigned int cluster = read_start_cluster(entry);
    if (!is_valid_cluster(&boot_sector, cluster)) {
        return false;
    }

    unsigned int entries_per_cluster = get_entries_per_cluster(&boot_sector);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!entries) {
        return false;
    }

    uint32_t traversed = 0;
    uint32_t limit = get_total_clusters(&boot_sector);
    while (is_valid_cluster(&boot_sector, cluster) && traversed++ < limit) {
        if (!read_cluster(&boot_sector, cluster, entries)) {
            free(entries);
            return false;
        }

        for (unsigned int j = 0; j < entries_per_cluster; j++) {
            if (entries[j].name[0] == 0x00) {
                free(entries);
                return true;
            }
            bool dot = entries[j].name[0] == '.' && entries[j].name[1] == ' ';
            bool dotdot = entries[j].name[0] == '.' &&
                          entries[j].name[1] == '.' &&
                          entries[j].name[2] == ' ';
            if (entries[j].name[0] == 0xE5 ||
                (entries[j].attr & 0x0F) == 0x0F || dot || dotdot) {
                continue;
            }
            free(entries);
            return false;
        }

        uint32_t next = get_next_cluster_in_chain(&boot_sector, cluster);
        if (next == INVALID_CLUSTER) {
            free(entries);
            return false;
        }
        if (is_end_of_cluster_chain(next)) {
            free(entries);
            return true;
        }
        cluster = next;
    }
    free(entries);
    return false;
}

static bool fat32_delete_dir_unlocked(const char* dirname) {
    if (!dirname || !fat32_is_valid_name(dirname) ||
        strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) {
        return false;
    }
    if (!fat32_prepare_write()) return false;
    // 1. Find the directory entry for the directory to delete
    struct fat32_dir_entry* entry = find_file_in_directory(dirname);
    if (entry == NULL) {
        printf("Directory not found.\n");
        return false;
    }
    if (!(entry->attr & ATTR_DIRECTORY) || (entry->attr & ATTR_READ_ONLY)) {
        free(entry);
        return false;
    }
    // 2. Check if the directory is empty
    if (!is_directory_empty(entry)) {
        printf("Directory is not empty.\n");
        free(entry);
        return false;
    }
    unsigned int start_cluster = read_start_cluster(entry);
    // Remove the name before freeing its storage, avoiding a visible dangling
    // entry if a later FAT update fails.
    if (!remove_entry_from_directory(&boot_sector, current_directory_cluster,
                                     entry)) {
        printf("Failed to remove the directory entry from the parent directory.\n");
        free(entry);
        return false;
    }
    if (!free_cluster_chain(&boot_sector, start_cluster)) {
        printf("Failed to free the directory's cluster chain.\n");
        free(entry);
        return false;
    }
    free(entry);
    
    // Sync FSInfo after directory deletion
    extern bool write_fsinfo(void);
    write_fsinfo();
    
    return true;
}

bool fat32_read_dir(const char* path) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_read_dir_unlocked(path);
    fat32_operation_end(flags);
    return result;
}

bool fat32_change_directory(const char* path) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_change_directory_unlocked(path);
    fat32_operation_end(flags);
    return result;
}

bool fat32_create_dir(const char* dirname) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_create_dir_unlocked(dirname);
    fat32_operation_end(flags);
    return result;
}

bool fat32_delete_dir(const char* dirname) {
    uint32_t flags = fat32_operation_begin();
    bool result = fat32_delete_dir_unlocked(dirname);
    fat32_operation_end(flags);
    return result;
}
