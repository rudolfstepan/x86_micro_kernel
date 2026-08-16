#include "fat12.h"
#include "../../drivers/block/fdd.h"
#include "../../lib/libc/stdio.h"
#include "../../lib/libc/stdlib.h"
#include "../../lib/libc/string.h"

// Additional constants
#define MAX_PATH_LENGTH 256


// Global structures and buffers
fat12_t* fat12 = NULL;
directory_entry* entries = NULL;
directory_entry* current_dir = NULL;
static directory_entry current_dir_storage;
uint8_t* buffer = NULL;
uint8_t current_fdd_drive = 0;

static bool fdc_read_logical_range(uint8_t drive, uint32_t logical_sector,
                                   uint32_t count, uint8_t *output);
static bool fdc_write_logical_range(uint8_t drive, uint32_t logical_sector,
                                    uint32_t count, const uint8_t *input);

static bool fat12_journal_read_sector(void *context, uint32_t sector,
                                      void *buffer) {
    (void)context;
    return fdc_read_logical_range(current_fdd_drive, sector, 1U, buffer);
}

static bool fat12_journal_write_sector(void *context, uint32_t sector,
                                       const void *buffer) {
    (void)context;
    return fdc_write_logical_range(current_fdd_drive, sector, 1U, buffer);
}

static bool fat12_journal_prepare(void) {
    if (!fat12 ||
        fat12->boot_sector.reserved_sectors < 23U + FAT12_REMAP_SPARE_COUNT +
                                              FAT12_REPLICA_RESERVED_SECTORS ||
        memcmp(fat12->boot_sector.fs_type, "REIST12", 7U) != 0 ||
        fat12->boot_sector.volume_id == 0U) return true;
    if (!fat12_journal_format(&fat12->journal, 2U, 3U, 4U,
                              fat12->boot_sector.volume_id)) return false;
    uint8_t primary[FAT12_SECTOR_SIZE], mirror[FAT12_SECTOR_SIZE];
    bool primary_ok = fdc_read_logical_range(current_fdd_drive, 2U, 1U,
                                             primary);
    bool mirror_ok = fdc_read_logical_range(current_fdd_drive, 3U, 1U,
                                            mirror);
    if (!primary_ok || !mirror_ok || !fat12_journal_load(&fat12->journal,
            fat12_journal_read_sector, NULL)) return false;
    if (!fat12_journal_recover(&fat12->journal, fat12_journal_read_sector,
                               fat12_journal_write_sector, NULL)) return false;
    if (!fat12_remap_format(&fat12->remap, 20U, 21U, 22U,
                            fat12->boot_sector.volume_id)) return false;
    if (!fat12_remap_load(&fat12->remap, fat12_journal_read_sector, NULL))
        return false;
    uint32_t replica_base = fat12->boot_sector.reserved_sectors -
                            FAT12_REPLICA_RESERVED_SECTORS;
    for (uint32_t slot = 0U; slot < FAT12_REPLICA_FILE_COUNT; ++slot) {
        uint32_t primary = replica_base + slot * FAT12_REPLICA_SLOT_SECTORS;
        if (!fat12_replica_init(&fat12->replicas[slot], primary,
                primary + 1U + FAT12_REPLICA_DATA_SECTORS,
                fat12->boot_sector.volume_id)) return false;
    }
    fat12->journal_enabled = true;
    fat12->remap_enabled = true;
    fat12->replica_enabled = true;
    return true;
}

static uint32_t fat12_remap_sector(uint32_t sector) {
    uint32_t replacement = 0U;
    return fat12 && fat12->remap_enabled &&
                   fat12_remap_lookup(&fat12->remap, sector, &replacement)
        ? replacement : sector;
}

static bool fat12_confirm_sector_defect(uint32_t sector) {
    uint8_t probe[FAT12_SECTOR_SIZE];
    for (uint32_t attempt = 0U; attempt < FAT12_DEFECT_CONFIRM_READS;
         ++attempt)
        if (fdc_read_logical_range(current_fdd_drive, sector, 1U, probe))
            return false;
    return true;
}

// Helper: read a single sector using DMA first, then fall back to no-DMA path
static bool fdc_read_with_fallback(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* out_buf) {
    if (fdc_read_sector(drive, head, track, sector, out_buf)) {
        return true;
    }
    printf("fdc_read_with_fallback: DMA read failed for %d/%d/%d, trying no-DMA fallback\n", track, head, sector);
    // if (fdc_read_sector_no_dma(drive, head, track, sector, out_buf)) {
    //     return true;
    // }
    // printf("fdc_read_with_fallback: no-DMA fallback also failed for %d/%d/%d\n", track, head, sector);
    return false;
}

// Function to calculate CHS from a logical sector number using boot sector geometry
void logical_to_chs(int logical_sector, int* track, int* head, int* sector) {
    if (!track || !head || !sector) return;
    uint16_t spt = FAT12_DEFAULT_SPT;
    uint16_t heads = FAT12_DEFAULT_HEADS;

    if (logical_sector < 0) {
        printf("ERROR: logical_to_chs called with negative sector %d\n", logical_sector);
        *track = *head = *sector = 0;
        return;
    }

    // Use boot sector values if available
    if (fat12 && fat12->boot_sector.sectors_per_track > 0 && fat12->boot_sector.heads > 0) {
        spt = fat12->boot_sector.sectors_per_track;
        heads = fat12->boot_sector.heads;
    }
    
    *track = logical_sector / (spt * heads);
    *head = (logical_sector / spt) % heads;
    *sector = (logical_sector % spt) + 1;  // Sectors are 1-based
    
    // Validate calculated values
    if (*sector < 1 || *sector > spt) {
        printf("WARNING: Invalid sector calculated: %d (max %d)\n", *sector, spt);
    }
}

// Validate cluster number is in valid FAT12 range
bool is_valid_cluster_fat12(int cluster) {
    if (cluster < FAT12_MIN_CLUSTER || cluster > FAT12_MAX_CLUSTER)
        return false;
    if (fat12 && fat12->boot_sector.sectors_per_cluster != 0) {
        uint32_t total_sectors = fat12->boot_sector.total_sectors ?
            fat12->boot_sector.total_sectors :
            fat12->boot_sector.total_sectors_large;
        if (total_sectors > (uint32_t)fat12->data_start) {
            uint32_t clusters = (total_sectors - fat12->data_start) /
                fat12->boot_sector.sectors_per_cluster;
            if ((uint32_t)cluster >= clusters + 2) return false;
        }
    }
    return true;
}

// Check if cluster marks end of chain
bool is_end_of_chain_fat12(int cluster) {
    return cluster >= FAT12_EOC_MIN && cluster <= FAT12_EOC_MAX;
}

// Validate FAT12 boot sector
bool validate_fat12_boot_sector(fat12_boot_sector* bs) {
    if (!bs) {
        printf("ERROR: NULL boot sector pointer\n");
        return false;
    }
    
    // Check boot signature
    if (bs->boot_sector_signature != FAT12_BOOT_SIGNATURE) {
        printf("ERROR: Invalid boot signature: 0x%04X (expected 0x%04X)\n", 
               bs->boot_sector_signature, FAT12_BOOT_SIGNATURE);
        return false;
    }
    
    // Validate bytes per sector
    // The FDD transport reads physical 512-byte sectors.  Accepting larger
    // BPB sector sizes would leave most buffers uninitialized and make all
    // FAT/directory offsets incorrect.
    if (bs->bytes_per_sector != FAT12_SECTOR_SIZE) {
        printf("ERROR: Invalid bytes_per_sector: %d\n", bs->bytes_per_sector);
        return false;
    }
    
    // Validate sectors per cluster (must be power of 2, max 128)
    if (bs->sectors_per_cluster == 0 || bs->sectors_per_cluster > 128 ||
        (bs->sectors_per_cluster & (bs->sectors_per_cluster - 1)) != 0) {
        printf("ERROR: Invalid sectors_per_cluster: %d\n", bs->sectors_per_cluster);
        return false;
    }
    
    // Validate FAT count
    if (bs->fat_count < 1 || bs->fat_count > 2) {
        printf("ERROR: Invalid FAT count: %d\n", bs->fat_count);
        return false;
    }
    
    // Validate root entry count
    if (bs->root_entry_count == 0) {
        printf("ERROR: Root entry count is zero\n");
        return false;
    }

    if (bs->reserved_sectors == 0 || bs->sectors_per_fat == 0 ||
        bs->sectors_per_track == 0 || bs->sectors_per_track > UINT8_MAX ||
        bs->heads == 0 || bs->heads > UINT8_MAX ||
        bs->hidden_sectors != 0) {
        printf("ERROR: Invalid FAT12 geometry or layout\n");
        return false;
    }
    
    // Calculate total clusters to verify it's FAT12
    uint32_t total_sectors = bs->total_sectors ? bs->total_sectors : bs->total_sectors_large;
    uint64_t addressable_sectors = (uint64_t)bs->sectors_per_track *
                                   bs->heads * 256u;
    if (total_sectors == 0 || total_sectors > addressable_sectors) {
        printf("ERROR: FAT12 volume exceeds FDC CHS address range\n");
        return false;
    }
    uint32_t root_dir_sectors = ((bs->root_entry_count * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
    uint32_t metadata_sectors = (uint32_t)bs->reserved_sectors +
        (uint32_t)bs->fat_count * bs->sectors_per_fat + root_dir_sectors;
    if (total_sectors <= metadata_sectors) {
        printf("ERROR: FAT12 metadata exceeds volume size\n");
        return false;
    }
    uint32_t data_sectors = total_sectors - metadata_sectors;
    uint32_t total_clusters = data_sectors / bs->sectors_per_cluster;
    
    if (total_clusters >= 4085) {
        printf("ERROR: Too many clusters for FAT12: %u (max 4084)\n", total_clusters);
        return false;
    }
    
    if (total_clusters < 2) {
        printf("ERROR: Too few clusters: %u\n", total_clusters);
        return false;
    }
    
    printf("FAT12 validation passed: %u clusters\n", total_clusters);
    return true;
}

// Valid filename check
bool is_valid_filename(const char* filename, size_t length) {
    const char* valid_chars = "!#$%&'()-@^_`{}~";
    for (size_t i = 0; i < length; i++) {
        if (!isalnum(filename[i]) && !strchr(valid_chars, filename[i])) {
            return false;
        }
    }
    return true;
}

// Read the FAT table and initialize fat12 structure
int read_fat12(uint8_t drive, fat12_t* fat12) {
    if (!fat12) return false;
    if (fat12->boot_sector.boot_sector_signature == 0xAA55) {
        printf("fat12 already initialized.\n");
        return true;
    }

    buffer = (uint8_t*)malloc(FAT12_SECTOR_SIZE);
    if (!buffer) {
        printf("Memory allocation failed for boot sector buffer.\n");
        return false;
    }

    printf("Attempting to read boot sector from drive %d (H:0, C:0, S:1)...\n", drive);

    // Debug: Verify drive state before reading
    printf("Drive state before boot sector read: Drive=%d\n", drive);
    if (!fdc_read_with_fallback(drive, 0, 0, 1, buffer)) {
        printf("Error reading boot sector from FDC (both DMA and no-DMA failed).\n");

        // Debug: Log failure details
        printf("Failed to read boot sector at H:0, C:0, S:1\n");
        free(buffer);
        buffer = NULL;
        return false;
    }

    printf("Boot sector read successful. First 16 bytes:\n");

    // Debug: Log boot sector content
    hex_dump(buffer, 16);
    for (int i = 0; i < 16; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
    
    printf("Boot signature bytes at offset 510-511: 0x%02X%02X\n", 
           buffer[510], buffer[511]);

    memcpy(&fat12->boot_sector, buffer, sizeof(fat12_boot_sector));
    free(buffer);
    buffer = NULL;

        // Validate boot sector
    if (!validate_fat12_boot_sector(&fat12->boot_sector)) {
        printf("Boot sector validation failed.\n");
        return false;
    }

    // Geometry and layout from BPB (no hard-coded sector sizes/macros)
    const uint16_t bps   = fat12->boot_sector.bytes_per_sector;
    const uint16_t spt   = fat12->boot_sector.sectors_per_track;
    const uint16_t heads = fat12->boot_sector.heads;
    const uint16_t rsvd  = fat12->boot_sector.reserved_sectors;
    const uint8_t  fats  = fat12->boot_sector.fat_count;
    const uint16_t spf   = fat12->boot_sector.sectors_per_fat;
    const uint16_t root_entries = fat12->boot_sector.root_entry_count;

    // Number of sectors occupied by the root directory
    const uint32_t root_dir_sectors = ((root_entries * 32u) + (bps - 1u)) / bps;

    // Compute region starts in logical sectors (LBA, from start of volume)
    fat12->fat_start     = rsvd;                             // first FAT
    fat12->root_dir_start= fat12->fat_start + (fats * spf);  // root dir starts after all FATs
    fat12->data_start    = fat12->root_dir_start + root_dir_sectors;

    printf("Boot sector geometry:\n");
    printf("  BPS=%u, SPT=%u, Heads=%u\n", bps, spt, heads);
    printf("  Reserved=%u, FATs=%u, SPF=%u, RootEntries=%u\n", rsvd, fats, spf, root_entries);

    printf("Calculated sectors:\n");
    printf("  fat_start: %u\n", fat12->fat_start);
    printf("  root_dir_start: %u\n", fat12->root_dir_start);
    printf("  data_start: %u\n", fat12->data_start);

    if (!fat12_journal_prepare()) {
        printf("ERROR: FAT12 journal recovery failed; refusing mount\n");
        return false;
    }

    // Allocate and load FAT table into memory
    uint32_t fat_size_bytes = (uint32_t)spf * bps;
    fat12->fat = (uint8_t*)malloc(fat_size_bytes);
    if (!fat12->fat) {
        printf("ERROR: Failed to allocate memory for FAT table (%u bytes)\n", fat_size_bytes);
        return false;
    }

    printf("Loading FAT table (%u sectors, %u bytes)...\n", spf, fat_size_bytes);
    if (!fdc_read_logical_range(drive, fat12->fat_start, spf, fat12->fat)) {
        printf("ERROR: Failed to read FAT sectors\n");
        free(fat12->fat);
        fat12->fat = NULL;
        return false;
    }

    printf("FAT table loaded successfully\n");
    printf("fat12 initialized: FAT Start Sector: %u, Root Directory Start Sector: %u, Data Region Start Sector: %u\n",
           fat12->fat_start, fat12->root_dir_start, fat12->data_start);
    return true;


    // Debug: Show boot sector geometry
    printf("Boot sector geometry:\n");
    printf("  bytes_per_sector: %u\n", fat12->boot_sector.bytes_per_sector);
    printf("  sectors_per_track: %u\n", fat12->boot_sector.sectors_per_track);
    printf("  heads: %u\n", fat12->boot_sector.heads);
    printf("  reserved_sectors: %u\n", fat12->boot_sector.reserved_sectors);
    printf("  fat_count: %u\n", fat12->boot_sector.fat_count);
    printf("  sectors_per_fat: %u\n", fat12->boot_sector.sectors_per_fat);
    printf("  root_entry_count: %u\n", fat12->boot_sector.root_entry_count);
    
    fat12->fat_start = fat12->boot_sector.reserved_sectors;
    fat12->root_dir_start = fat12->fat_start + (fat12->boot_sector.fat_count * fat12->boot_sector.sectors_per_fat);
    fat12->data_start = fat12->root_dir_start + (fat12->boot_sector.root_entry_count * FAT12_ROOT_ENTRY_SIZE / FAT12_SECTOR_SIZE);
    
    printf("Calculated sectors:\n");
    printf("  fat_start: %d\n", fat12->fat_start);
    printf("  root_dir_start: %d\n", fat12->root_dir_start);
    printf("  data_start: %d\n", fat12->data_start);
    
    // Allocate and load FAT table into memory
    uint32_t fat_size = fat12->boot_sector.sectors_per_fat *
                        fat12->boot_sector.bytes_per_sector;
    fat12->fat = (uint8_t*)malloc(fat_size);
    if (!fat12->fat) {
        printf("ERROR: Failed to allocate memory for FAT table (%u bytes)\n", fat_size);
        return false;
    }
    
    // Read FAT table sectors
    printf("Loading FAT table (%u sectors, %u bytes)...\n", 
           fat12->boot_sector.sectors_per_fat, fat_size);
    
        for (int i = 0; i < fat12->boot_sector.sectors_per_fat; i++) {
        int logical_sector = fat12->fat_start + i;
        int track, head, sector;
        logical_to_chs(logical_sector, &track, &head, &sector);
        
        uint8_t* fat_buffer = fat12->fat + (i * FAT12_SECTOR_SIZE);
        if (!fdc_read_with_fallback(drive, head, track, sector, fat_buffer)) {
            printf("ERROR: Failed to read FAT sector %d (both DMA and no-DMA)\n", i);
            free(fat12->fat);
            fat12->fat = NULL;
            return false;
        }
    }
    
    printf("FAT table loaded successfully\n");

    printf("fat12 initialized: FAT Start Sector: %d, Root Directory Start Sector: %d, Data Region Start Sector: %d\n", 
           fat12->fat_start, fat12->root_dir_start, fat12->data_start);
    return true;
}

// Initialize fat12 and load root directory
bool fat12_init_fs(uint8_t drive) {
    current_fdd_drive = drive; // Set the current drive
    
    printf("fat12_init_fs: Attempting to allocate %d bytes for fat12_t\n", sizeof(fat12_t));
    
    // Free existing allocation if any
    if (fat12 != NULL) {
        printf("Freeing existing fat12 structure at %p\n", fat12);
        if (fat12->fat != NULL) {
            free(fat12->fat);
            fat12->fat = NULL;
        }
        free(fat12);
        fat12 = NULL;
    }
    
    // Initialize fat12 structure
    fat12 = (fat12_t*)malloc(sizeof(fat12_t));
    if (!fat12) {
        printf("Memory allocation failed for fat12 structure.\n");
        printf("Requested size: %d bytes\n", sizeof(fat12_t));
        return false;
    }
    printf("fat12 structure allocated at %p\n", fat12);
    memset(fat12, 0, sizeof(fat12_t));

    if (!read_fat12(drive, fat12)) {
        printf("Failed to read boot sector for FAT12.\n");
        free(fat12);
        fat12 = NULL;
        return false;
    }
    return true;
}

// Function to calculate and fetch the next cluster in fat12
int get_next_cluster(int current_cluster) {
    // Validate inputs
    if (!fat12) {
        printf("ERROR: FAT12 not initialized\n");
        return -1;
    }
    
    if (!fat12->fat) {
        printf("ERROR: FAT table not loaded\n");
        return -1;
    }
    
    if (!is_valid_cluster_fat12(current_cluster)) {
        printf("ERROR: Invalid cluster number: %d\n", current_cluster);
        return -1;
    }
    
    // Calculate offset in FAT table (FAT12 uses 12 bits per entry)
    uint32_t offset = ((uint32_t)current_cluster * 3u) / 2u;
    
    // Bounds check
    uint32_t fat_size = fat12->boot_sector.sectors_per_fat *
                        fat12->boot_sector.bytes_per_sector;
    if (offset + 1 >= fat_size) {
        printf("ERROR: FAT offset out of bounds: %u (max %u)\n", offset, fat_size);
        return -1;
    }
    
    // Read 12-bit FAT entry (even/odd cluster handling)
    uint16_t next_cluster;
    if ((current_cluster & 1) == 0) {
        // Even cluster: use low 12 bits
        next_cluster = (fat12->fat[offset] | (fat12->fat[offset + 1] << 8)) & 0x0FFF;
    } else {
        // Odd cluster: use high 12 bits
        next_cluster = ((fat12->fat[offset] >> 4) | (fat12->fat[offset + 1] << 4)) & 0x0FFF;
    }
    
    // Check for end of chain or bad cluster
    if (is_end_of_chain_fat12(next_cluster)) {
        return -1;  // End of cluster chain
    }
    
    if (next_cluster == FAT12_BAD_CLUSTER) {
        printf("ERROR: Bad cluster encountered: 0x%03X\n", next_cluster);
        return -1;
    }
    
    if (next_cluster == FAT12_FREE_CLUSTER ||
        next_cluster == FAT12_RESERVED_CLUSTER ||
        (next_cluster >= 0xFF0 && next_cluster <= 0xFF6)) {
        printf("ERROR: Invalid cluster in chain: 0x%03X\n", next_cluster);
        return -1;
    }
    
    return next_cluster;
}

// Function to extract date from fat12 format
void extract_date(uint16_t fat_date, int* day, int* month, int* year) {
    *day = fat_date & 0x1F;
    *month = (fat_date >> 5) & 0x0F;
    *year = ((fat_date >> 9) & 0x7F) + 1980;
}

// Function to extract time from fat12 format
void extract_time(uint16_t fat_time, int* hours, int* minutes, int* seconds) {
    *seconds = (fat_time & 0x1F) * 2;
    *minutes = (fat_time >> 5) & 0x3F;
    *hours = (fat_time >> 11) & 0x1F;
}

// Read directory entries (root or subdirectory)
int fat12_read_dir_entries(directory_entry* dir) {
    if (!fat12 || !fat12->fat) return -1;
    int entries_found = 0;
    uint8_t* local_buffer = NULL;

    // Free existing entries allocation if any
    if (entries != NULL) {
        free(entries);
        entries = NULL;
    }

    const uint16_t bps = fat12->boot_sector.bytes_per_sector;
    const uint16_t spc = fat12->boot_sector.sectors_per_cluster;
    const uint16_t root_entries = fat12->boot_sector.root_entry_count;
    const uint32_t root_dir_sectors =
        ((root_entries * 32u) + (bps - 1u)) / bps;
    uint32_t entries_capacity = dir == NULL ? root_entries :
        ((uint32_t)spc * bps) / sizeof(directory_entry);
    if (entries_capacity == 0) return -1;

    entries = (directory_entry*)malloc(entries_capacity * sizeof(directory_entry));
    if (!entries) {
        printf("Memory allocation failed for directory entries.\n");
        return -1;
    }

    if (dir == NULL) {
        // --- ROOT DIRECTORY ---
        // Read all root dir sectors into a contiguous buffer
        const uint32_t root_bytes = root_dir_sectors * bps;
        local_buffer = (uint8_t*)malloc(root_bytes);
        if (!local_buffer) {
            printf("Memory allocation failed for root dir buffer.\n");
            free(entries); entries = NULL;
            return -1;
        }

        for (uint32_t si = 0; si < root_dir_sectors;) {
            int ls = (int)fat12->root_dir_start + (int)si;
            int t, h, s;
            logical_to_chs(ls, &t, &h, &s);
            uint32_t batch = fat12->boot_sector.sectors_per_track -
                             (uint32_t)s + 1U;
            if (batch > root_dir_sectors - si) batch = root_dir_sectors - si;
            if (!fdc_read_logical_range(current_fdd_drive, (uint32_t)ls,
                                        batch, local_buffer + si * bps)) {
                printf("Error reading root directory sector %u (logical %d).\n", si, ls);
                free(entries); entries = NULL;
                free(local_buffer);
                return -1;
            }
            bool end_of_directory = false;
            for (uint32_t batch_index = 0; batch_index < batch; ++batch_index) {
                directory_entry *sector_entries = (directory_entry*)(
                    local_buffer + (si + batch_index) * bps);
                uint32_t entries_per_sector = bps / sizeof(directory_entry);
                for (uint32_t i = 0; i < entries_per_sector; ++i) {
                    if ((uint8_t)sector_entries[i].filename[0] == 0x00) {
                        end_of_directory = true;
                        break;
                    }
                }
                if (end_of_directory) break;
            }
            if (end_of_directory) break;
            si += batch;
        }

        // Parse 32-byte entries
        const uint32_t total_entries = root_bytes / sizeof(directory_entry);
        for (uint32_t i = 0; i < total_entries &&
             (uint32_t)entries_found < entries_capacity; i++) {
            directory_entry* e = (directory_entry*)(local_buffer + i * sizeof(directory_entry));

            uint8_t first = (uint8_t)e->filename[0];
            if (first == 0x00) break;            // no more entries
            if (first == 0xE5) continue;         // deleted entry
            if ((e->attributes & 0x0F) == 0x0F ||
                (e->attributes & FILE_ATTR_VOLUME_LABEL))
                continue;

            entries[entries_found++] = *e;
        }
    } else {
        // --- SUBDIRECTORY from its cluster ---
        int cluster = dir->first_cluster_low;

        // Root directory in FAT12 has cluster number = 0 (not used)
        if (cluster == 0) {
            printf("Detected root directory entry (cluster=0), switching to root read mode.\n");
            free(entries);
            entries = NULL;
            return fat12_read_dir_entries(NULL); // Redirect to root reader
        }

        if (!is_valid_cluster_fat12(cluster)) {
            printf("ERROR: Invalid subdirectory cluster: %d (not in valid FAT12 range)\n", cluster);
            free(entries);
            entries = NULL;
            return -1;
        }

        const uint32_t cluster_bytes = (uint32_t)spc * bps;

        local_buffer = (uint8_t*)malloc(cluster_bytes);
        if (!local_buffer) {
            printf("Memory allocation failed for subdir buffer.\n");
            free(entries);
            entries = NULL;
            return -1;
        }

        uint32_t total_sectors = fat12->boot_sector.total_sectors ?
            fat12->boot_sector.total_sectors :
            fat12->boot_sector.total_sectors_large;
        uint32_t maximum_clusters =
            (total_sectors - fat12->data_start) / spc;
        uint32_t maximum_entries = maximum_clusters *
            (cluster_bytes / sizeof(directory_entry));
        uint32_t clusters_seen = 0;
        while (is_valid_cluster_fat12(cluster) &&
               clusters_seen++ < maximum_clusters) {
            uint32_t cluster_sector = fat12->data_start +
                (uint32_t)(cluster - 2) * spc;
            if (!fdc_read_logical_range(current_fdd_drive, cluster_sector,
                                        spc, local_buffer)) {
                printf("Error reading subdirectory cluster %d.\n", cluster);
                free(entries); entries = NULL;
                free(local_buffer);
                return -1;
            }

            // Parse 32-byte entries from this cluster
            const uint32_t total_entries = cluster_bytes / sizeof(directory_entry);
            for (uint32_t i = 0; i < total_entries; i++) {
                directory_entry* e = (directory_entry*)(local_buffer + i * sizeof(directory_entry));

                uint8_t first = (uint8_t)e->filename[0];
                if (first == 0x00) { // end of dir in this cluster
                    // could still have further clusters; FAT says 0x00 can appear before EOC,
                    // but commonly signals end—safe to stop cluster loop as well
                    cluster = -1;
                    break;
                }
                if (first == 0xE5) continue;         // deleted
                if ((e->attributes & 0x0F) == 0x0F ||
                    (e->attributes & FILE_ATTR_VOLUME_LABEL))
                    continue;

                if ((uint32_t)entries_found == entries_capacity) {
                    uint32_t new_capacity = entries_capacity * 2u;
                    if (new_capacity < entries_capacity ||
                        new_capacity > maximum_entries) {
                        new_capacity = maximum_entries;
                    }
                    if (new_capacity <= entries_capacity) {
                        free(entries);
                        entries = NULL;
                        free(local_buffer);
                        return -1;
                    }
                    directory_entry* resized = (directory_entry*)realloc(
                        entries, new_capacity * sizeof(directory_entry));
                    if (!resized) {
                        free(entries);
                        entries = NULL;
                        free(local_buffer);
                        return -1;
                    }
                    entries = resized;
                    entries_capacity = new_capacity;
                }

                entries[entries_found++] = *e;
            }

            if (cluster < 0) break;

            cluster = get_next_cluster(cluster);  // next in chain
            if (cluster < 0) break;               // EOC or error
        }

        if (is_valid_cluster_fat12(cluster) &&
            clusters_seen >= maximum_clusters) {
            printf("ERROR: Cycle detected in FAT12 directory chain\n");
            free(entries);
            entries = NULL;
            free(local_buffer);
            return -1;
        }
    }

    if (local_buffer) free(local_buffer);
    return entries_found;
}

// Print directory entries in DOS-like format
void print_dir_entries(directory_entry* dir, int entries_found) {
    printf(" Volume in drive A has no label\n");
    printf(" Directory of %s\n\n", dir == NULL ? "\\" : *dir->filename == 0 ? "\\" : (char*)dir->filename);
    printf("FILENAME   EXT    SIZE     DATE       TIME     TYPE\n");
    printf("----------------------------------------------------\n");

    for (int i = 0; i < entries_found; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & (0x10 | 0x20))) continue;

        char filename[9] = {0};
        char extension[4] = {0};
        strncpy(filename, (const char*)entry->filename, 8);
        strncpy(extension, (const char*)entry->extension, 3);

        int day, month, year, hours, minutes, seconds;
        extract_date(entry->last_write_date, &day, &month, &year);
        extract_time(entry->last_write_time, &hours, &minutes, &seconds);

        if (entry->attributes & 0x10) {  // Directory
            printf("%-8s   %-3s   <DIR>    %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, day, month, year, hours, minutes, seconds);
        } else {  // File
            printf("%-8s   %-3s   %8u  %02d-%02d-%04d  %02d:%02d:%02d\n",
                   filename, extension, entry->file_size, day, month, year, hours, minutes, seconds);
        }
    }
    printf("\n");
}

// Change to a new directory if it exists
bool fat12_change_directory(const char* relative_path) {
    if (!relative_path || *relative_path == '\0') return false;
    int num_entries = fat12_read_dir_entries(current_dir);
    if (num_entries < 0) {
        printf("Failed to read directory.\n");
        return false;
    }

    for (int i = 0; i < num_entries; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (!(entry->attributes & 0x10)) continue;  // Only directories

        char entry_name[9] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        entry_name[8] = '\0';  // Ensure null-termination

        char trimmed_name[9] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);

        size_t relative_length = strlen(relative_path);
        if (strlen(trimmed_name) == relative_length &&
            strncasecmp(trimmed_name, relative_path, relative_length) == 0) {
            // Keep an owned copy.  `entries` is replaced on every directory
            // read, so retaining a pointer into it is a use-after-free.
            current_dir_storage = *entry;
            current_dir = &current_dir_storage;
            printf("Changed directory to %s\n", relative_path);
            return true;
        }
    }
    printf("Directory not found: %s\n", relative_path);
    return false;
}

// Open a file in the current directory
fat12_file* fat12_open_file(const char* filename, const char* mode) {
    // Validate inputs
    if (!filename || !mode) {
        printf("ERROR: NULL filename or mode\n");
        return NULL;
    }
    
    if (!fat12 || !fat12->fat) {
        printf("ERROR: FAT12 not initialized\n");
        return NULL;
    }
    
    // Validate filename length
    size_t filename_len = strlen(filename);
    if (filename_len == 0 || filename_len > 12) {  // 8.3 format max
        printf("ERROR: Invalid filename length: %zu\n", filename_len);
        return NULL;
    }
    
    int num_entries = fat12_read_dir_entries(current_dir);
    if (num_entries < 0) {
        printf("Failed to read directory contents.\n");
        return NULL;
    }

    directory_entry* file_entry = NULL;
    for (int i = 0; i < num_entries; i++) {
        directory_entry* entry = &entries[i];
        if ((unsigned char)entry->filename[0] == 0x00) break;
        if ((unsigned char)entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x10) continue;  // Skip directories

        char entry_name[9] = {0};
        char entry_ext[4] = {0};
        strncpy(entry_name, (const char*)entry->filename, 8);
        strncpy(entry_ext, (const char*)entry->extension, 3);

        char trimmed_name[13] = {0};
        char trimmed_ext[4] = {0};
        str_trim_spaces(entry_name, trimmed_name, 8);
        str_trim_spaces(entry_ext, trimmed_ext, 3);

        if (trimmed_ext[0] != '\0') {
            strncat(trimmed_name, ".", 1);
            strncat(trimmed_name, trimmed_ext, 3);
        }

        if (strlen(trimmed_name) == filename_len &&
            strncasecmp(trimmed_name, filename, filename_len) == 0) {
            file_entry = entry;
            break;
        }
    }

    if (file_entry == NULL) {
        printf("File not found: %s\n", filename);
        return NULL;
    }
    
    // Validate file entry
    if (file_entry->file_size != 0 &&
        !is_valid_cluster_fat12(file_entry->first_cluster_low)) {
        printf("ERROR: File has invalid cluster: %u\n", file_entry->first_cluster_low);
        return NULL;
    }
    
    if (file_entry->file_size == 0) {
        printf("WARNING: File size is zero: %s\n", filename);
    }

    fat12_file* file = (fat12_file*)malloc(sizeof(fat12_file));
    if (file == NULL) {
        printf("Failed to allocate memory for file structure.\n");
        return NULL;
    }
    
    // Initialize all fields
    memset(file, 0, sizeof(fat12_file));

    // File contents are streamed sector by sector by fat12_read_file; keeping
    // a second full-size allocation here only wastes memory and was leaked by
    // several legacy callers.
    file->base = NULL;
    file->ptr = NULL;
    file->start_cluster = file_entry->first_cluster_low;
    file->size = file_entry->file_size;
    file->position = 0;
    strncpy((char*)file->name, filename, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    strncpy(file->mode, mode, sizeof(file->mode) - 1);
    file->mode[sizeof(file->mode) - 1] = '\0';

    printf("File opened: %s, Size: %d bytes\n", file->name, file->size);
    return file;
}

// Print the contents of a file
void print_file_content(fat12_file* file) {
    if (file == NULL) {
        printf("Invalid file handle.\n");
        return;
    }

    // Calculate the size of the buffer based on the size of the file
    size_t buffer_size = file->size; // Use the file size as the buffer size directly

    // Allocate the buffer
    char* buffer = (char*)malloc(sizeof(char) * buffer_size);

    if (buffer == NULL) {
        printf("Memory allocation failed.\n");
        return;
    }
    memset(buffer, 0, buffer_size);

    int bytes_read = fat12_read_file(file, buffer, buffer_size, file->size);
    if (bytes_read > 0) {
        printf("File contents:\n%s\n", buffer);
        hex_dump((unsigned char*)buffer, buffer_size);
    } else {
        printf("Failed to read file content.\n");
    }

    free(buffer);
}

// Read directory based on the specified path
bool fat12_read_dir(const char* path) {
    printf("-----Reading directory: %s-----\n", path ? path : "(current directory)");

    int num_entries = 0;
    if(current_dir == NULL) {
        // read root directory
        num_entries = fat12_read_dir_entries(NULL);
        if (num_entries < 0) {
            printf("Failed to load root directory.\n");
            return false;
        }
    }else {
        // read subdirectory
        num_entries = fat12_read_dir_entries(current_dir);
        if (num_entries < 0) {
            printf("Failed to load subdirectory.\n");
            return false;
        }
    }

    print_dir_entries(current_dir, num_entries);
    return true;
}

int fat12_read_file(fat12_file* file, void* buffer, unsigned int buffer_size, unsigned int bytes_to_read) {
    // Validate inputs
    if (file == NULL || buffer == NULL) {
        printf("ERROR: Invalid file handle or buffer.\n");
        return 0;
    }
    
    if (!fat12 || !fat12->fat) {
        printf("ERROR: FAT12 not initialized or FAT table not loaded.\n");
        return 0;
    }
    
    if (buffer_size == 0 || bytes_to_read == 0) {
        return 0;
    }
    
    // Ensure we don't read beyond the end of the file or buffer
    if (file->position >= file->size) {
        return 0;
    }
    if (bytes_to_read > file->size - file->position) {
        bytes_to_read = file->size - file->position;
    }
    if (bytes_to_read > buffer_size) {
        bytes_to_read = buffer_size;
    }
    
    if (bytes_to_read == 0) {
        return 0;  // Nothing to read
    }

    if (!is_valid_cluster_fat12(file->start_cluster)) {
        printf("ERROR: File has invalid start cluster: %u\n", file->start_cluster);
        return 0;
    }

    unsigned int bytes_read = 0;
    unsigned int current_cluster = file->start_cluster;
    unsigned int clusterSize = FAT12_SECTOR_SIZE * fat12->boot_sector.sectors_per_cluster;
    unsigned int clusters_to_skip = file->position / clusterSize;
    unsigned int offset_in_cluster = file->position % clusterSize;
    uint32_t total_sectors = fat12->boot_sector.total_sectors ?
        fat12->boot_sector.total_sectors : fat12->boot_sector.total_sectors_large;
    uint32_t cluster_limit = (total_sectors - fat12->data_start) /
                             fat12->boot_sector.sectors_per_cluster;
    if (clusters_to_skip >= cluster_limit) return 0;

    // Seek to the cluster containing file->position.
    for (unsigned int i = 0; i < clusters_to_skip; i++) {
        int next = get_next_cluster((int)current_cluster);
        if (next < 0) {
            return 0;
        }
        current_cluster = (unsigned int)next;
    }

    unsigned char* sectorBuffer = (unsigned char*)malloc(FAT12_SECTOR_SIZE);
    if (sectorBuffer == NULL) {
        printf("Memory allocation failed for sector buffer.\n");
        return 0;
    }

    // Read loop
    uint32_t clusters_seen = clusters_to_skip;
    while (bytes_read < bytes_to_read && clusters_seen++ < cluster_limit &&
           is_valid_cluster_fat12((int)current_cluster) &&
           current_cluster != FAT12_BAD_CLUSTER) {
        // Calculate the first sector of the current cluster
        unsigned int firstSectorOfCluster = fat12->data_start + (current_cluster - 2) * fat12->boot_sector.sectors_per_cluster;

        /* Image-created files normally have consecutive cluster chains. Read
         * complete adjacent clusters in one logical range so program loading
         * uses track-sized DMA requests instead of one IRQ per sector. */
        unsigned int complete_clusters =
            (bytes_to_read - bytes_read) / clusterSize;
        if (offset_in_cluster == 0 && complete_clusters != 0) {
            unsigned int run = 1;
            unsigned int last_cluster = current_cluster;
            while (run < complete_clusters &&
                   clusters_seen + run - 1U < cluster_limit) {
                int next_run_cluster = get_next_cluster((int)last_cluster);
                if (next_run_cluster != (int)last_cluster + 1) break;
                last_cluster = (unsigned int)next_run_cluster;
                ++run;
            }
            uint32_t run_sectors = run *
                fat12->boot_sector.sectors_per_cluster;
            if (!fdc_read_logical_range(
                    current_fdd_drive, firstSectorOfCluster, run_sectors,
                    (uint8_t*)buffer + bytes_read)) {
                free(sectorBuffer);
                return bytes_read;
            }
            unsigned int copied = run * clusterSize;
            bytes_read += copied;
            file->position += copied;
            clusters_seen += run - 1U;
            if (bytes_read >= bytes_to_read) break;
            int next = get_next_cluster((int)last_cluster);
            if (next < 0) break;
            current_cluster = (unsigned int)next;
            offset_in_cluster = 0;
            continue;
        }

        // Read each sector in the cluster
        unsigned int first_sector_index = offset_in_cluster / FAT12_SECTOR_SIZE;
        unsigned int first_sector_offset = offset_in_cluster % FAT12_SECTOR_SIZE;
        for (unsigned int i = first_sector_index;
             i < fat12->boot_sector.sectors_per_cluster && bytes_read < bytes_to_read;
             i++) {
            unsigned int logical_sector = firstSectorOfCluster + i;
            int track, head, sector;
            logical_to_chs(logical_sector, &track, &head, &sector);

            // Read the sector
            if (!fdc_read_with_fallback(current_fdd_drive, head, track, sector, sectorBuffer)) {
                printf("Error reading file sector at track %d, head %d, sector %d (both DMA and no-DMA failed).\n", track, head, sector);
                free(sectorBuffer);
                return bytes_read; // Return bytes read so far on failure
            }

            // Calculate how many bytes to copy from the sector
            unsigned int offset = (i == first_sector_index) ? first_sector_offset : 0;
            unsigned int remaining = FAT12_SECTOR_SIZE - offset;
            unsigned int bytes_to_copy = (bytes_to_read - bytes_read < remaining) ? (bytes_to_read - bytes_read) : remaining;

            // Copy the bytes from the sector buffer to the main buffer
            memcpy((unsigned char*)buffer + bytes_read, sectorBuffer + offset, bytes_to_copy);
            bytes_read += bytes_to_copy;
            file->position += bytes_to_copy;

            if (bytes_read >= bytes_to_read) {
                break;
            }
        }

        if (bytes_read >= bytes_to_read) {
            break;
        }

        offset_in_cluster = 0;
        int next = get_next_cluster((int)current_cluster);
        if (next < 0) {
            // End of chain or error.
            break;
        }
        current_cluster = (unsigned int)next;
    }

    // Null-terminate the buffer if applicable (only for text data)
    if (bytes_read < buffer_size) {
        ((char*)buffer)[bytes_read] = '\0';
    }

    free(sectorBuffer);

    return bytes_read;
}

/* Read a contiguous logical range using the largest transfer that still fits
 * on the current track. Fall back to single sectors for conservative FDCs. */
static bool fdc_read_logical_range(uint8_t drive, uint32_t logical_sector,
                                   uint32_t count, uint8_t *output) {
    uint16_t sectors_per_track = fat12 && fat12->boot_sector.sectors_per_track ?
        fat12->boot_sector.sectors_per_track : FAT12_DEFAULT_SPT;
    while (count != 0) {
        int track, head, sector;
        logical_to_chs((int)logical_sector, &track, &head, &sector);
        uint32_t batch = sectors_per_track - (uint32_t)sector + 1U;
        if (batch > count) batch = count;
        if (batch > 18U) batch = 18U;
        if (!fdc_read_sectors(drive, (uint8_t)head, (uint8_t)track,
                              (uint8_t)sector, (uint8_t)batch, output)) {
            for (uint32_t index = 0; index < batch; ++index) {
                int fallback_track, fallback_head, fallback_sector;
                logical_to_chs((int)(logical_sector + index),
                               &fallback_track, &fallback_head,
                               &fallback_sector);
                if (!fdc_read_with_fallback(
                        drive, (uint8_t)fallback_head,
                        (uint8_t)fallback_track, (uint8_t)fallback_sector,
                        output + index * FAT12_SECTOR_SIZE)) return false;
            }
        }
        logical_sector += batch;
        output += batch * FAT12_SECTOR_SIZE;
        count -= batch;
    }
    return true;
}

static bool fdc_write_logical_range(uint8_t drive, uint32_t logical_sector,
                                    uint32_t count, const uint8_t *input) {
    uint16_t sectors_per_track = fat12 && fat12->boot_sector.sectors_per_track ?
        fat12->boot_sector.sectors_per_track : FAT12_DEFAULT_SPT;
    while (count != 0) {
        int track, head, sector;
        logical_to_chs((int)logical_sector, &track, &head, &sector);
        uint32_t batch = sectors_per_track - (uint32_t)sector + 1U;
        if (batch > count) batch = count;
        if (batch > 18U) batch = 18U;
        if (!fdc_write_sectors(drive, (uint8_t)head, (uint8_t)track,
                               (uint8_t)sector, (uint8_t)batch, input)) {
            for (uint32_t index = 0; index < batch; ++index) {
                int fallback_track, fallback_head, fallback_sector;
                logical_to_chs((int)(logical_sector + index),
                               &fallback_track, &fallback_head,
                               &fallback_sector);
                if (!fdd_write_sector(
                        drive, (uint8_t)fallback_head,
                        (uint8_t)fallback_track, (uint8_t)fallback_sector,
                        input + index * FAT12_SECTOR_SIZE)) return false;
            }
        }
        logical_sector += batch;
        input += batch * FAT12_SECTOR_SIZE;
        count -= batch;
    }
    return true;
}

uint32_t fat12_cluster_count(void) {
    if (!fat12 || fat12->boot_sector.sectors_per_cluster == 0) return 0;
    uint32_t total = fat12->boot_sector.total_sectors ?
        fat12->boot_sector.total_sectors :
        fat12->boot_sector.total_sectors_large;
    if (total <= (uint32_t)fat12->data_start) return 0;
    uint32_t clusters = (total - (uint32_t)fat12->data_start) /
                        fat12->boot_sector.sectors_per_cluster;
    uint32_t fat_entries = ((uint32_t)fat12->boot_sector.sectors_per_fat *
                            fat12->boot_sector.bytes_per_sector * 2U) / 3U;
    if (fat_entries <= 2U) return 0;
    if (clusters > fat_entries - 2U) clusters = fat_entries - 2U;
    return clusters;
}

uint16_t fat12_get_fat_entry(uint16_t cluster) {
    if (!fat12 || !fat12->fat || cluster < FAT12_MIN_CLUSTER ||
        (uint32_t)cluster >= fat12_cluster_count() + 2U) {
        return FAT12_BAD_CLUSTER;
    }
    uint32_t offset = (uint32_t)cluster + cluster / 2U;
    uint16_t packed = (uint16_t)fat12->fat[offset] |
                      ((uint16_t)fat12->fat[offset + 1U] << 8);
    return (cluster & 1U) ? (uint16_t)(packed >> 4) :
                            (uint16_t)(packed & 0x0FFFU);
}

bool fat12_set_fat_entry(uint16_t cluster, uint16_t value) {
    if (!fat12 || !fat12->fat || cluster < FAT12_MIN_CLUSTER ||
        (uint32_t)cluster >= fat12_cluster_count() + 2U) return false;
    uint32_t offset = (uint32_t)cluster + cluster / 2U;
    value &= 0x0FFFU;
    if ((cluster & 1U) == 0) {
        fat12->fat[offset] = (uint8_t)value;
        fat12->fat[offset + 1U] =
            (uint8_t)((fat12->fat[offset + 1U] & 0xF0U) | (value >> 8));
    } else {
        fat12->fat[offset] =
            (uint8_t)((fat12->fat[offset] & 0x0FU) | (value << 4));
        fat12->fat[offset + 1U] = (uint8_t)(value >> 4);
    }
    return true;
}

bool fat12_read_logical_sectors(uint32_t logical_sector, uint32_t count,
                                void* output) {
    if (!fat12 || !output || count == 0) return false;
    uint32_t total = fat12->boot_sector.total_sectors ?
        fat12->boot_sector.total_sectors :
        fat12->boot_sector.total_sectors_large;
    if (logical_sector >= total || count > total - logical_sector) return false;
    if (!fat12->remap_enabled)
        return fdc_read_logical_range(current_fdd_drive, logical_sector, count,
                                      (uint8_t*)output);
    uint8_t *bytes = (uint8_t*)output;
    for (uint32_t index = 0U; index < count; ++index) {
        uint32_t physical = fat12_remap_sector(logical_sector + index);
        if (!fdc_read_logical_range(current_fdd_drive, physical, 1U,
                                    bytes + index * FAT12_SECTOR_SIZE))
            return false;
    }
    return true;
}

bool fat12_write_logical_sectors(uint32_t logical_sector, uint32_t count,
                                 const void* input) {
    if (!fat12 || !input || count == 0) return false;
    uint32_t total = fat12->boot_sector.total_sectors ?
        fat12->boot_sector.total_sectors :
        fat12->boot_sector.total_sectors_large;
    if (logical_sector >= total || count > total - logical_sector) return false;
    if (fat12->journal_enabled && count > FAT12_JOURNAL_MAX_ENTRIES)
        return false;
    if (!fat12->journal_enabled)
        return fdc_write_logical_range(current_fdd_drive, logical_sector, count,
                                       (const uint8_t*)input);
    static uint64_t journal_sequence = 1U;
    if (++journal_sequence == 0U) return false;
    if (!fat12_journal_begin(&fat12->journal, journal_sequence,
                             fat12_journal_read_sector,
                             fat12_journal_write_sector, NULL)) return false;
    const uint8_t *bytes = (const uint8_t*)input;
    for (uint32_t index = 0U; index < count; ++index) {
        uint8_t old_sector[FAT12_SECTOR_SIZE];
        uint8_t verify[FAT12_SECTOR_SIZE];
        uint32_t physical = fat12_remap_sector(logical_sector + index);
        if (!fat12_read_logical_sectors(logical_sector + index, 1U,
                                        old_sector) ||
            !fat12_journal_record(&fat12->journal, logical_sector + index,
                                   old_sector, fat12_journal_read_sector,
                                   fat12_journal_write_sector,
                                   NULL) ||
            !fdc_write_logical_range(current_fdd_drive, physical, 1U,
                                     bytes + index * FAT12_SECTOR_SIZE) ||
            !fdc_read_logical_range(current_fdd_drive, physical, 1U, verify) ||
            memcmp(bytes + index * FAT12_SECTOR_SIZE, verify,
                   FAT12_SECTOR_SIZE) != 0)
            return false;
    }
    return fat12_journal_commit(&fat12->journal, fat12_journal_read_sector,
                                fat12_journal_write_sector,
                                NULL);
}

bool fat12_sync_fat(void) {
    if (!fat12 || !fat12->fat) return false;
    uint32_t sectors = fat12->boot_sector.sectors_per_fat;
    for (uint32_t copy = 0; copy < fat12->boot_sector.fat_count; ++copy) {
        uint32_t start = (uint32_t)fat12->fat_start + copy * sectors;
        for (uint32_t offset = 0U; offset < sectors;) {
            uint32_t batch = sectors - offset;
            if (batch > FAT12_JOURNAL_MAX_ENTRIES)
                batch = FAT12_JOURNAL_MAX_ENTRIES;
            if (!fat12_write_logical_sectors(start + offset, batch,
                    fat12->fat + offset * FAT12_SECTOR_SIZE)) return false;
            offset += batch;
        }
    }
    return true;
}

bool fat12_quarantine_data_cluster(uint16_t cluster) {
    if (!fat12 || !fat12->fat || cluster < FAT12_MIN_CLUSTER ||
        (uint32_t)cluster >= fat12_cluster_count() + 2U) return false;
    if (fat12_get_fat_entry(cluster) == FAT12_BAD_CLUSTER) return true;
    uint32_t first_sector = (uint32_t)fat12->data_start +
        ((uint32_t)cluster - FAT12_MIN_CLUSTER) *
        fat12->boot_sector.sectors_per_cluster;
    bool confirmed = false;
    for (uint32_t index = 0U;
         index < fat12->boot_sector.sectors_per_cluster; ++index) {
        if (!fat12_confirm_sector_defect(first_sector + index)) continue;
        confirmed = true;
        break;
    }
    if (!confirmed) return false;
    uint16_t previous = fat12_get_fat_entry(cluster);
    if (!fat12_set_fat_entry(cluster, FAT12_BAD_CLUSTER)) return false;
    if (fat12_sync_fat()) return true;
    (void)fat12_set_fat_entry(cluster, previous);
    return false;
}

bool fat12_install_sector_remap(uint32_t bad_sector,
                                const void *recovered_sector) {
    if (!fat12 || !recovered_sector || !fat12->remap_enabled ||
        fat12->boot_sector.reserved_sectors < 23U + FAT12_REMAP_SPARE_COUNT +
                                              FAT12_REPLICA_RESERVED_SECTORS ||
        bad_sector < (uint32_t)fat12->fat_start ||
        bad_sector >= (uint32_t)fat12->data_start ||
        !fat12_confirm_sector_defect(bad_sector)) return false;
    uint32_t existing = 0U;
    if (fat12_remap_lookup(&fat12->remap, bad_sector, &existing)) return false;
    uint32_t spare_start = fat12->boot_sector.reserved_sectors -
                           FAT12_REPLICA_RESERVED_SECTORS -
                           FAT12_REMAP_SPARE_COUNT;
    uint32_t replacement = 0U;
    for (uint32_t candidate = spare_start;
         candidate < spare_start + FAT12_REMAP_SPARE_COUNT; ++candidate) {
        bool used = false;
        for (uint32_t index = 0U; index < fat12->remap.header.entry_count;
             ++index)
            if (fat12->remap.entries[index].replacement_sector == candidate)
                used = true;
        if (!used) { replacement = candidate; break; }
    }
    if (replacement == 0U) return false;
    uint8_t verify[FAT12_SECTOR_SIZE];
    if (!fdc_write_logical_range(current_fdd_drive, replacement, 1U,
                                 recovered_sector) ||
        !fdc_read_logical_range(current_fdd_drive, replacement, 1U, verify) ||
        memcmp(recovered_sector, verify, sizeof(verify)) != 0) return false;
    return fat12_remap_add(&fat12->remap, bad_sector, replacement,
                           fat12_journal_read_sector,
                           fat12_journal_write_sector, NULL);
}

static int fat12_critical_replica_slot(const char *name) {
    static const char *const names[FAT12_REPLICA_FILE_COUNT] = {
        "REIST.CFG", "STORAGE.CFG", "BOOT.CFG"
    };
    if (!fat12_is_critical_name(name)) return -1;
    for (uint32_t slot = 0U; slot < FAT12_REPLICA_FILE_COUNT; ++slot)
        if (strlen(name) == strlen(names[slot]) &&
            strncasecmp(name, names[slot], strlen(names[slot])) == 0)
            return (int)slot;
    return -1;
}

bool fat12_publish_critical_replica(const char *name, const void *data,
                                    size_t length) {
    int slot = fat12_critical_replica_slot(name);
    if (!fat12 || !fat12->replica_enabled || slot < 0 || data == NULL ||
        length == 0U || length > FAT12_REPLICA_MAX_BYTES) return false;
    fat12_replica_t *replica = &fat12->replicas[slot];
    uint64_t sequence = 1U;
    if (fat12_replica_load(replica, fat12_journal_read_sector, NULL)) {
        sequence = replica->primary_header.sequence >
                   replica->mirror_header.sequence
            ? replica->primary_header.sequence : replica->mirror_header.sequence;
        if (sequence == UINT64_MAX) return false;
        ++sequence;
    }
    return fat12_replica_publish_persistent(replica, data, length, sequence,
        fat12_journal_read_sector, fat12_journal_write_sector, NULL);
}

bool fat12_read_critical_replica(const char *name, void *output,
                                 size_t capacity, size_t *length_out) {
    int slot = fat12_critical_replica_slot(name);
    if (!fat12 || !fat12->replica_enabled || slot < 0 || output == NULL ||
        length_out == NULL) return false;
    fat12_replica_t *replica = &fat12->replicas[slot];
    return fat12_replica_load(replica, fat12_journal_read_sector, NULL) &&
           fat12_replica_select(replica, output, capacity, length_out);
}

// Close a file and free its resources
void fat12_close_file(fat12_file* file) {
    if (file) {
        if (file->base) {
            free(file->base);
            file->base = NULL;
        }
        free(file);
    }
}

// Cleanup FAT12 filesystem and free all resources
void fat12_cleanup(void) {
    if (fat12) {
        if (fat12->fat) {
            free(fat12->fat);
            fat12->fat = NULL;
        }
        free(fat12);
        fat12 = NULL;
    }
    
    if (entries) {
        free(entries);
        entries = NULL;
    }
    
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    
    current_dir = NULL;
    memset(&current_dir_storage, 0, sizeof(current_dir_storage));
    printf("FAT12 cleanup complete\n");
}
