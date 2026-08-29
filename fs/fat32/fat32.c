/**
 * @file fs/fat32/fat32.c
 * @brief FAT32-Probe, Mount und Basissektorzugriff.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: BPB und Mediengeometrie werden vor jedem berechneten Zugriff validiert.
 */
#include "fat32.h"
#include "include/reist/utf.h"
#include "lib/libc/stdio.h"
#include "drivers/bus/drives.h"
#ifndef KERNEL_HOST_TEST
#include "include/kernel/panic.h"
#include "kernel/sched/mutex.h"
#endif

// fat32 file system implementation
// The file is divided into:
// fat32.c: Contains the implementation of the FAT32 filesystem functions.
// fat32_dir.c: Contains the implementation of the FAT32 directory functions.
// fat32_files.c: Contains the implementation of the FAT32 file functions.
// fat32_cluster.c: Contains the implementation of the FAT32 I/O functions.

unsigned int current_directory_cluster = 2; // Default root directory cluster for FAT32
struct fat32_boot_sector boot_sector;
struct fat32_fsinfo fsinfo;

unsigned short ata_base_address;
bool ata_is_master;
unsigned int partition_lba_offset = 0; // LBA offset for partitioned disks (0 for whole disk)
bool fsinfo_valid = false; // Track if FSInfo is loaded and valid
bool fat32_write_supported = false;
fat32_context_sync_hook_t fat32_context_sync_hook;
fat32_context_mutation_hook_t fat32_context_mutation_hook;

#define FAT32_FAT_CACHE_ENTRIES 8U
typedef struct {
    unsigned short base;
    uint32_t partition_lba;
    uint32_t sector;
    bool is_master;
    bool valid;
    uint8_t data[SECTOR_SIZE];
} fat32_fat_cache_entry_t;

/* This is a metadata traversal cache, not a verification cache.  A journal
 * transaction has an in-memory pending view which may later abort, so active
 * transactions always bypass this storage.  Every FAT32 mutation invalidates
 * it before staging its first side effect. */
static fat32_fat_cache_entry_t fat32_fat_cache[FAT32_FAT_CACHE_ENTRIES];
static uint32_t fat32_fat_cache_next;

void fat32_fat_cache_invalidate(void) {
    for (uint32_t index = 0U; index < FAT32_FAT_CACHE_ENTRIES; ++index)
        fat32_fat_cache[index].valid = false;
    fat32_fat_cache_next = 0U;
}

static bool fat32_read_fat_sector(uint32_t sector, void *buffer) {
    if (buffer == NULL) return false;
    if (ata_journal_transaction_active()) {
        return ata_read_sector(ata_base_address, sector, buffer,
                               ata_is_master);
    }
    for (uint32_t index = 0U; index < FAT32_FAT_CACHE_ENTRIES; ++index) {
        fat32_fat_cache_entry_t *entry = &fat32_fat_cache[index];
        if (entry->valid && entry->base == ata_base_address &&
            entry->is_master == ata_is_master &&
            entry->partition_lba == partition_lba_offset &&
            entry->sector == sector) {
            memcpy(buffer, entry->data, SECTOR_SIZE);
            return true;
        }
    }
    uint8_t data[SECTOR_SIZE];
    if (!ata_read_sector(ata_base_address, sector, data, ata_is_master))
        return false;
    fat32_fat_cache_entry_t *entry =
        &fat32_fat_cache[fat32_fat_cache_next];
    fat32_fat_cache_next =
        (fat32_fat_cache_next + 1U) % FAT32_FAT_CACHE_ENTRIES;
    entry->base = ata_base_address;
    entry->partition_lba = partition_lba_offset;
    entry->sector = sector;
    entry->is_master = ata_is_master;
    memcpy(entry->data, data, SECTOR_SIZE);
    entry->valid = true;
    memcpy(buffer, data, SECTOR_SIZE);
    return true;
}

#define FAT32_OPERATION_LOCK_TIMEOUT_MS 10000U
#ifndef KERNEL_HOST_TEST
static kernel_mutex_t fat32_operation_mutex = KERNEL_MUTEX_INIT;
#endif

uint32_t fat32_operation_begin(void) {
#ifdef KERNEL_HOST_TEST
    return 0;
#else
    KASSERT(kernel_mutex_lock_for(&fat32_operation_mutex,
                                  FAT32_OPERATION_LOCK_TIMEOUT_MS) == 0);
    return 0;
#endif
}

void fat32_operation_end(uint32_t interrupt_flags) {
    if (fat32_context_sync_hook) fat32_context_sync_hook();
#ifdef KERNEL_HOST_TEST
    (void)interrupt_flags;
#else
    (void)interrupt_flags;
    kernel_mutex_unlock(&fat32_operation_mutex);
#endif
}

bool fat32_prepare_write(void) {
    if (!fat32_write_supported || boot_sector.total_sectors_32 == 0U ||
        boot_sector.reserved_sector_count == 0U) {
        return false;
    }
    if (ata_journal_is_attached(ata_base_address, ata_is_master,
                                partition_lba_offset,
                                boot_sector.total_sectors_32)) {
        return true;
    }

    /* The ATA compatibility journal is currently a single global binding.
     * A different mounted volume may have replaced it since this FAT32
     * context was activated. Reattach and recover the exact current extent
     * before allowing the first sector side effect. */
    if (!ata_journal_attach(ata_base_address, ata_is_master,
                            partition_lba_offset,
                            boot_sector.total_sectors_32,
                            boot_sector.reserved_sector_count) ||
        !ata_journal_is_attached(ata_base_address, ata_is_master,
                                 partition_lba_offset,
                                 boot_sector.total_sectors_32)) {
        fat32_write_supported = false;
        return false;
    }
    return true;
}

bool fat32_write_sector(unsigned int lba, void* buffer) {
    if (buffer == NULL || !fat32_prepare_write()) return false;
    fat32_fat_cache_invalidate();
    bool written =
        ata_write_sector(ata_base_address, lba, buffer, ata_is_master);
    if (!written) return false;
    if (fat32_context_mutation_hook) fat32_context_mutation_hook();
    return true;
}

// Forward declarations
bool read_fsinfo(void);
bool write_fsinfo(void);

int fat32_init_fs_at(unsigned short base, bool is_master, uint32_t partition_lba) {
    struct fat32_boot_sector candidate_boot;
    struct fat32_fsinfo candidate_fsinfo;
    bool candidate_fsinfo_valid = false;
    bool candidate_write_supported = false;

    fat32_fat_cache_invalidate();
    memset(&candidate_boot, 0, sizeof(candidate_boot));
    memset(&candidate_fsinfo, 0, sizeof(candidate_fsinfo));

    if (!ata_read_sector(base, partition_lba, &candidate_boot, is_master)) {
        return FAILURE;
    }

    // Always validate media-derived geometry.  The implementation operates on
    // 512-byte ATA sectors, so larger logical BPB sectors are not supported.
    if (candidate_boot.boot_sector_signature != 0xAA55) {
        printf("+++ Invalid boot signature: 0x%04X +++\n",
               candidate_boot.boot_sector_signature);
        return FAILURE;
    }

    if (candidate_boot.bytes_per_sector != SECTOR_SIZE) {
        printf("+++ Invalid bytes per sector: %u +++\n",
               candidate_boot.bytes_per_sector);
        return FAILURE;
    }

    if (candidate_boot.sectors_per_cluster == 0 ||
        candidate_boot.sectors_per_cluster > 128 ||
        (candidate_boot.sectors_per_cluster &
         (candidate_boot.sectors_per_cluster - 1)) != 0) {
        printf("+++ Invalid sectors per cluster: %u +++\n",
               candidate_boot.sectors_per_cluster);
        return FAILURE;
    }

    if (candidate_boot.reserved_sector_count == 0 ||
        candidate_boot.number_of_fats == 0 ||
        candidate_boot.number_of_fats > 2 ||
        candidate_boot.fat_size_32 == 0 ||
        candidate_boot.total_sectors_32 == 0 ||
        candidate_boot.total_sectors_16 != 0 ||
        candidate_boot.root_entry_count != 0 ||
        candidate_boot.fat_size_16 != 0) {
        printf("+++ Invalid FAT32 BPB layout +++\n");
        return FAILURE;
    }
    if ((candidate_boot.flags & 0xFF70u) != 0 ||
        ((candidate_boot.flags & 0x0080u) != 0 &&
         (candidate_boot.flags & 0x000Fu) >= candidate_boot.number_of_fats)) {
        printf("+++ Invalid FAT32 FAT-selection flags +++\n");
        return FAILURE;
    }

    uint64_t metadata_sectors =
        (uint64_t)candidate_boot.reserved_sector_count +
        (uint64_t)candidate_boot.number_of_fats * candidate_boot.fat_size_32;
    if (metadata_sectors >= candidate_boot.total_sectors_32) {
        printf("+++ FAT32 metadata exceeds volume +++\n");
        return FAILURE;
    }

    uint32_t cluster_count =
        (candidate_boot.total_sectors_32 - (uint32_t)metadata_sectors) /
        candidate_boot.sectors_per_cluster;
    uint64_t fat_entries = ((uint64_t)candidate_boot.fat_size_32 *
                            SECTOR_SIZE) / sizeof(uint32_t);
    uint64_t volume_end = (uint64_t)partition_lba +
                          candidate_boot.total_sectors_32;
    if (cluster_count == 0 || cluster_count >= FAT32_RESERVED_MIN - 1u ||
        fat_entries < (uint64_t)cluster_count + 2u ||
        volume_end > (uint64_t)UINT32_MAX + 1u ||
        candidate_boot.root_cluster < 2 ||
        candidate_boot.root_cluster > cluster_count + 1u) {
        printf("+++ Invalid root cluster: %u +++\n",
               candidate_boot.root_cluster);
        return FAILURE;
    }

    /* Recover an interrupted single-sector update before consuming any
     * mutable FAT/FSInfo/directory metadata.  Foreign volumes without the
     * explicit REIST journal marker remain untouched. */
    if (!ata_journal_attach(base, is_master, partition_lba,
                            candidate_boot.total_sectors_32,
                            candidate_boot.reserved_sector_count)) {
        printf("+++ REIST journal recovery failed; refusing mount +++\n");
        return FAILURE;
    }
    candidate_write_supported = ata_journal_is_attached(
        base, is_master, partition_lba, candidate_boot.total_sectors_32);

    /* Read and validate optional metadata into local storage as well.  None of
     * the global selectors or cached structures are published until the BPB
     * is known to be usable, so a failed volume switch is transactional. */
    if (candidate_boot.fs_info != 0 && candidate_boot.fs_info != 0xFFFF &&
        candidate_boot.fs_info < candidate_boot.reserved_sector_count &&
        ata_read_sector(base, partition_lba + candidate_boot.fs_info,
                        &candidate_fsinfo, is_master) &&
        candidate_fsinfo.lead_signature == 0x41615252 &&
        candidate_fsinfo.struct_signature == 0x61417272 &&
        candidate_fsinfo.trail_signature == 0xAA550000) {
        if (candidate_fsinfo.free_cluster_count != 0xFFFFFFFF &&
            candidate_fsinfo.free_cluster_count > cluster_count) {
            candidate_fsinfo.free_cluster_count = 0xFFFFFFFF;
        }
        if (candidate_fsinfo.next_free_cluster != 0xFFFFFFFF &&
            (candidate_fsinfo.next_free_cluster < 2 ||
             candidate_fsinfo.next_free_cluster > cluster_count + 1u)) {
            candidate_fsinfo.next_free_cluster = 0xFFFFFFFF;
        }
        candidate_fsinfo_valid = true;
    }

    boot_sector = candidate_boot;
    fsinfo = candidate_fsinfo;
    fsinfo_valid = candidate_fsinfo_valid;
    ata_is_master = is_master;
    ata_base_address = base;
    partition_lba_offset = partition_lba;
    fat32_write_supported = candidate_write_supported;
    current_directory_cluster = candidate_boot.root_cluster;

    return SUCCESS;
}

int fat32_init_fs(unsigned short base, bool is_master) {
    return fat32_init_fs_at(base, is_master, 0);
}

// ------------------------------------------------------------------
// read_fsinfo
// Reads the FSInfo sector and validates it
// ------------------------------------------------------------------
bool read_fsinfo(void) {
    uint32_t fsinfo_sector = partition_lba_offset + boot_sector.fs_info;
    
    if (!ata_read_sector(ata_base_address, fsinfo_sector, &fsinfo, ata_is_master)) {
        printf("Error: Failed to read FSInfo sector\n");
        return false;
    }
    
    // Validate signatures
    if (fsinfo.lead_signature != 0x41615252 || 
        fsinfo.struct_signature != 0x61417272 || 
        fsinfo.trail_signature != 0xAA550000) {
        printf("Error: Invalid FSInfo signatures (lead=0x%08X, struct=0x%08X, trail=0x%08X)\n",
               fsinfo.lead_signature, fsinfo.struct_signature, fsinfo.trail_signature);
        return false;
    }
    
    uint32_t total_clusters = get_total_clusters(&boot_sector);
    if (fsinfo.free_cluster_count != 0xFFFFFFFF &&
        fsinfo.free_cluster_count > total_clusters) {
        fsinfo.free_cluster_count = 0xFFFFFFFF;
    }
    if (fsinfo.next_free_cluster != 0xFFFFFFFF &&
        !is_valid_cluster(&boot_sector, fsinfo.next_free_cluster)) {
        fsinfo.next_free_cluster = 0xFFFFFFFF;
    }

    fsinfo_valid = true;
    printf("FSInfo loaded: free_clusters=%u, next_free=%u\n", 
           fsinfo.free_cluster_count, fsinfo.next_free_cluster);
    return true;
}

// ------------------------------------------------------------------
// write_fsinfo
// Writes the FSInfo sector back to disk
// ------------------------------------------------------------------
bool write_fsinfo(void) {
    if (!fsinfo_valid) {
        return false; // Don't write if FSInfo wasn't loaded successfully
    }
    
    uint32_t fsinfo_sector = partition_lba_offset + boot_sector.fs_info;
    
    if (!fat32_write_sector(fsinfo_sector, &fsinfo)) {
        printf("Error: Failed to write FSInfo sector\n");
        return false;
    }
    
    // Verify the write by reading back the FSInfo sector
    struct fat32_fsinfo verify_fsinfo;
    if (!ata_read_sector(ata_base_address, fsinfo_sector, &verify_fsinfo, ata_is_master)) {
        printf("Error: Failed to read back FSInfo sector for verification\n");
        return false;
    }
    
    // Verify critical fields (signatures and counters)
    if (verify_fsinfo.lead_signature != fsinfo.lead_signature ||
        verify_fsinfo.struct_signature != fsinfo.struct_signature ||
        verify_fsinfo.trail_signature != fsinfo.trail_signature ||
        verify_fsinfo.free_cluster_count != fsinfo.free_cluster_count ||
        verify_fsinfo.next_free_cluster != fsinfo.next_free_cluster) {
        printf("Error: FSInfo write verification failed - data mismatch\n");
        return false;
    }
    
    return true;
}

// ------------------------------------------------------------------
// update_fsinfo_free_count
// Updates the free cluster count (call after alloc/free operations)
// ------------------------------------------------------------------
void update_fsinfo_free_count(int delta) {
    if (!fsinfo_valid) {
        return; // FSInfo not available
    }
    
    // If count is unknown, don't update
    if (fsinfo.free_cluster_count == 0xFFFFFFFF) {
        return;
    }
    
    uint32_t total_clusters = get_total_clusters(&boot_sector);
    if (fsinfo.free_cluster_count > total_clusters) {
        fsinfo.free_cluster_count = total_clusters;
    }
    if (delta < 0) {
        uint32_t amount = (uint32_t)(-(int64_t)delta);
        fsinfo.free_cluster_count = fsinfo.free_cluster_count < amount ?
            0 : fsinfo.free_cluster_count - amount;
    } else {
        uint32_t amount = (uint32_t)delta;
        fsinfo.free_cluster_count =
            amount > total_clusters - fsinfo.free_cluster_count ?
            total_clusters : fsinfo.free_cluster_count + amount;
    }
}

bool fat32_is_valid_short_name(const char* name) {
    if (!name || name[0] == '\0') return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return true;

    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    bool extension = false;
    const char* extra = "!#$%&'()-@^_`{}~";
    for (uint32_t i = 0; name[i] != '\0'; i++) {
        unsigned char character = (unsigned char)name[i];
        if (character > 0x7FU) return false;
        if (character == '.') {
            if (extension || base_length == 0) return false;
            extension = true;
            continue;
        }
        if (!isalnum(character) && !strchr(extra, character)) return false;
        if (!extension) {
            if (++base_length > 8) return false;
        } else if (++extension_length > 3) {
            return false;
        }
    }
    return base_length != 0 && (!extension || extension_length != 0);
}

bool fat32_is_valid_name(const char* name) {
    if (!name || name[0] == '\0') return false;
    size_t length = strlen(name);
    if (length == 0 || length > FAT32_MAX_LFN_CHARS ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    uint16_t units[FAT32_MAX_LFN_CHARS];
    size_t unit_count = 0U;
    if (!reist_utf8_to_utf16(name, length, units,
                             FAT32_MAX_LFN_CHARS, &unit_count) ||
        unit_count == 0U || unit_count > FAT32_MAX_LFN_CHARS ||
        units[unit_count - 1U] == (uint16_t)' ' ||
        units[unit_count - 1U] == (uint16_t)'.') return false;
    for (size_t i = 0; i < unit_count; i++) {
        uint16_t value = units[i];
        if (value < 0x20U || value == (uint16_t)'"' ||
            value == (uint16_t)'*' || value == (uint16_t)'/' ||
            value == (uint16_t)':' || value == (uint16_t)'<' ||
            value == (uint16_t)'>' || value == (uint16_t)'?' ||
            value == (uint16_t)'\\' || value == (uint16_t)'|') {
            return false;
        }
    }
    return true;
}

void fat32_format_short_name(const struct fat32_dir_entry* entry,
                             char output[13]) {
    if (!entry || !output) return;
    size_t used = 0;
    for (size_t i = 0; i < 8 && entry->name[i] != ' '; i++) {
        char value = (char)entry->name[i];
        if ((entry->nt_res & 0x08U) && value >= 'A' && value <= 'Z')
            value = (char)(value + ('a' - 'A'));
        output[used++] = value;
    }
    if (entry->name[8] != ' ') {
        output[used++] = '.';
        for (size_t i = 8; i < 11 && entry->name[i] != ' '; i++) {
            char value = (char)entry->name[i];
            if ((entry->nt_res & 0x10U) && value >= 'A' && value <= 'Z')
                value = (char)(value + ('a' - 'A'));
            output[used++] = value;
        }
    }
    output[used] = '\0';
}

uint8_t fat32_short_name_checksum(const uint8_t name[11]) {
    uint8_t checksum = 0;
    if (!name) return 0;
    for (size_t i = 0; i < 11; i++) {
        checksum = (uint8_t)(((checksum & 1U) << 7) |
                             ((checksum & 0xFEU) >> 1));
        checksum = (uint8_t)(checksum + name[i]);
    }
    return checksum;
}

// ------------------------------------------------------------------
// Compares a FAT32 8.3 formatted name with a regular string
// Returns 0 if the names match, -1 otherwise
// ------------------------------------------------------------------
int compare_names(const char* fat_name, const char* regular_name) {
    if (!fat_name || !fat32_is_valid_short_name(regular_name)) return -1;
    unsigned char formatted[11];
    convert_to_83_format(formatted, regular_name);
    return memcmp(fat_name, formatted, sizeof(formatted)) == 0 ? 0 : -1;
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
        if (isalnum((unsigned char)src[i]) ||
            strchr("!#$%&'()-@^_`{}~", src[i])) {
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
            if (isalnum((unsigned char)src[i]) ||
                strchr("!#$%&'()-@^_`{}~", src[i])) {
                dest[j++] = toupper((unsigned char)src[i]);
            }
            i++;
        }
    }
}

void set_fat32_time(unsigned short* time, unsigned short* date) {
    // Read current date and time from RTC
    int year, month, day;
    int hours, minutes, seconds;
    
    extern void read_date(int* year, int* month, int* day);
    extern void read_time(int* hours, int* minutes, int* seconds);
    
    read_date(&year, &month, &day);
    read_time(&hours, &minutes, &seconds);
    
    // Set FAT32 time format: bits 15-11=hours, 10-5=minutes, 4-0=seconds/2
    if (time) {
        *time = (hours << 11) | (minutes << 5) | (seconds / 2);
    }
    
    // Set FAT32 date format: bits 15-9=year-1980, 8-5=month, 4-0=day
    if (date) {
        // Ensure year is within valid range (1980-2107)
        int fat_year = year - 1980;
        if (fat_year < 0) fat_year = 0;
        if (fat_year > 127) fat_year = 127;
        
        *date = (fat_year << 9) | (month << 5) | day;
    }
}

bool fat32_fat_is_mirrored(const struct fat32_boot_sector* boot_sector) {
    return boot_sector && (boot_sector->flags & 0x0080u) == 0;
}

unsigned int fat32_active_fat_index(const struct fat32_boot_sector* boot_sector) {
    if (!boot_sector || fat32_fat_is_mirrored(boot_sector)) return 0;
    return boot_sector->flags & 0x000Fu;
}

unsigned int read_fat_entry(struct fat32_boot_sector* boot_sector, unsigned int cluster) {
    // Validate cluster number
    if (!is_valid_cluster(boot_sector, cluster)) {
        printf("Error: Invalid cluster %u in read_fat_entry\n", cluster);
        return INVALID_CLUSTER;
    }
    
    unsigned int fat_offset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int fat_sector = partition_lba_offset +
        boot_sector->reserved_sector_count +
        fat32_active_fat_index(boot_sector) * boot_sector->fat_size_32 +
        (fat_offset / boot_sector->bytes_per_sector);
    unsigned int ent_offset = fat_offset % boot_sector->bytes_per_sector;
    // Buffer to read a part of the FAT
    unsigned char buffer[SECTOR_SIZE];
    // Read the sector of the FAT that contains the current cluster's entry
    if (!fat32_read_fat_sector(fat_sector, buffer)) {
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
    return mark_cluster_in_fat(boot_sector, cluster, value);
}

bool remove_entry_from_directory(struct fat32_boot_sector* boot_sector, unsigned int parent_cluster, struct fat32_dir_entry* entry) {
    unsigned int entries_per_cluster = get_entries_per_cluster(boot_sector);
    size_t entries_size = entries_per_cluster * sizeof(struct fat32_dir_entry);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)malloc(entries_size);
    if (!entries) {
        return false;
    }
    unsigned int current_cluster = parent_cluster;
    unsigned int traversed = 0;
    unsigned int cluster_limit = get_total_clusters(boot_sector);
    struct { uint32_t cluster; uint32_t index; }
        slots[FAT32_MAX_LFN_ENTRIES + 1U];
    uint32_t slot_count = 0;

    while (is_valid_cluster(boot_sector, current_cluster) &&
           traversed++ < cluster_limit) {
        if (!read_cluster(boot_sector, current_cluster, entries)) {
            free(entries);
            return false;
        }

        for (unsigned int i = 0; i < entries_per_cluster; ++i) {
            if (entries[i].name[0] == 0xE5 || entries[i].name[0] == 0x00) {
                slot_count = 0;
                if (entries[i].name[0] == 0x00) break;
                continue;
            }
            if (entries[i].attr == ATTR_LONG_NAME) {
                if (slot_count == FAT32_MAX_LFN_ENTRIES) slot_count = 0;
                slots[slot_count].cluster = current_cluster;
                slots[slot_count].index = i;
                slot_count++;
                continue;
            }
            if (memcmp(&entries[i], entry, sizeof(struct fat32_dir_entry)) == 0) {
                slots[slot_count].cluster = current_cluster;
                slots[slot_count].index = i;
                slot_count++;
                uint32_t first = 0;
                while (first < slot_count) {
                    uint32_t target = slots[first].cluster;
                    if (!read_cluster(boot_sector, target, entries)) {
                        free(entries);
                        return false;
                    }
                    uint32_t cursor = first;
                    while (cursor < slot_count &&
                           slots[cursor].cluster == target) {
                        entries[slots[cursor].index].name[0] = 0xE5;
                        cursor++;
                    }
                    if (!write_cluster(boot_sector, target, entries)) {
                        if (!read_cluster(boot_sector, target, entries)) {
                            free(entries);
                            return false;
                        }
                        for (uint32_t verify = first; verify < cursor;
                             verify++) {
                            if (entries[slots[verify].index].name[0] != 0xE5) {
                                free(entries);
                                return false;
                            }
                        }
                    }
                    first = cursor;
                }
                free(entries);
                return true;
            }
            slot_count = 0;
        }

        unsigned int next = get_next_cluster_in_chain(boot_sector, current_cluster);
        if (next == INVALID_CLUSTER || is_end_of_cluster_chain(next)) {
            break;
        }
        current_cluster = next;
    }

    free(entries);
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
