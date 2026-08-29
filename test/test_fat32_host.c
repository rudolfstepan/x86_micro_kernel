/**
 * @file test/test_fat32_host.c
 * @brief Hostseitiger Regressionstest für fat32.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "fs/fat32/fat32.h"
#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_time.h"
#include "drivers/block/ata_journal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SECTORS 128u
#define ROOT_DIRECTORY_LBA 3u
#define FSINFO_FIRST_FREE 64u
#define FAT32_FAULT_CAMPAIGN_MAX_WRITES 384u
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t test_disk[TEST_SECTORS][SECTOR_SIZE];
static unsigned int root_read_failures;
static bool fail_directory_write_once;
static bool fail_any_write_once;
static int test_year = 2026;
static int test_month = 8;
static int test_day = 3;
static int test_hour = 12;
static int test_minute = 34;
static int test_second = 56;
static bool fail_directory_verify_once;
static int payload_writes_before_failure = -1;
static int fat_writes_before_verify_failure = -1;
static unsigned int fat_verify_read_failures;
static unsigned int fat_verify_failure_burst = 1;
static unsigned int fat_sector_reads;
static unsigned int cache_flushes;
static bool journal_volume_marked = true;
static bool journal_attached;
static unsigned int journal_attach_count;
static unsigned short journal_base;
static bool journal_master;
static uint32_t journal_partition_lba;
static uint32_t journal_volume_sectors;
static bool use_production_journal;
static ata_undo_journal_t host_journal;
static uint32_t root_directory_lba = ROOT_DIRECTORY_LBA;
static bool campaign_power_cut;
static bool campaign_fault_armed;
static unsigned int campaign_cut_after_write;
static unsigned int campaign_write_count;
static unsigned int campaign_flush_count;
static uint8_t campaign_baseline[TEST_SECTORS][SECTOR_SIZE];
static uint8_t campaign_committed[TEST_SECTORS][SECTOR_SIZE];
static uint32_t primary_fat_lba = 1U;
static uint32_t secondary_fat_lba = 2U;
drive_t* current_drive;

bool ata_flush_cache(unsigned short base, bool is_master) {
    (void)base;
    (void)is_master;
    if (campaign_power_cut) return false;
    ++cache_flushes;
    return true;
}

static bool sector_has_nonzero_data(const void* buffer) {
    const uint8_t* bytes = (const uint8_t*)buffer;
    for (size_t i = 0; i < SECTOR_SIZE; ++i) {
        if (bytes[i] != 0) return true;
    }
    return false;
}

static bool host_disk_read(unsigned short base, unsigned int lba, void* buffer,
                           bool is_master) {
    (void)base;
    (void)is_master;
    if (campaign_power_cut || lba >= TEST_SECTORS || !buffer) return false;
    if (lba == primary_fat_lba || lba == secondary_fat_lba) {
        ++fat_sector_reads;
    }
    if (lba == primary_fat_lba && fat_verify_read_failures > 0) {
        --fat_verify_read_failures;
        return false;
    }
    if (lba == root_directory_lba && root_read_failures > 0) {
        --root_read_failures;
        return false;
    }
    memcpy(buffer, test_disk[lba], SECTOR_SIZE);
    return true;
}

static bool host_raw_write(void *context, unsigned short base, uint32_t lba,
                           const void *buffer, bool is_master) {
    (void)context;
    (void)base;
    (void)is_master;
    if (campaign_power_cut || lba >= TEST_SECTORS || !buffer) return false;
    if (fail_any_write_once) {
        fail_any_write_once = false;
        return false;
    }
    if (lba == root_directory_lba && fail_directory_write_once) {
        fail_directory_write_once = false;
        return false;
    }
    if (lba > root_directory_lba && payload_writes_before_failure >= 0 &&
        sector_has_nonzero_data(buffer)) {
        if (payload_writes_before_failure-- == 0) {
            payload_writes_before_failure = -1;
            return false;
        }
    }
    memcpy(test_disk[lba], buffer, SECTOR_SIZE);
    if (campaign_fault_armed) {
        ++campaign_write_count;
        if (campaign_write_count == campaign_cut_after_write)
            campaign_power_cut = true;
    }
    if (lba == primary_fat_lba && fat_writes_before_verify_failure >= 0) {
        if (fat_writes_before_verify_failure-- == 0) {
            fat_writes_before_verify_failure = -1;
            fat_verify_read_failures = fat_verify_failure_burst;
            fat_verify_failure_burst = 1;
        }
    }
    if (lba == root_directory_lba && fail_directory_verify_once) {
        fail_directory_verify_once = false;
        root_read_failures = 1;
    }
    return true;
}

static bool host_raw_read(void *context, unsigned short base, uint32_t lba,
                          void *buffer, bool is_master) {
    (void)context;
    return host_disk_read(base, lba, buffer, is_master);
}

static bool host_raw_flush(void *context, unsigned short base,
                           bool is_master) {
    (void)context;
    (void)base;
    (void)is_master;
    if (campaign_power_cut) return false;
    ++campaign_flush_count;
    return true;
}

static bool host_commit_begin(void *context, unsigned short base,
                              bool is_master) {
    (void)context;
    (void)base;
    (void)is_master;
    return !campaign_power_cut;
}

static bool host_commit_end(void *context, unsigned short base,
                            bool is_master, bool commit) {
    bool flushed = host_raw_flush(context, base, is_master);
    return commit && flushed;
}

static bool host_raw_write_sectors(void *context, unsigned short base,
                                   uint32_t lba, uint32_t count,
                                   const void *buffer, bool is_master) {
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!host_raw_write(context, base, lba + index,
                            bytes + index * SECTOR_SIZE, is_master))
            return false;
    }
    return true;
}

static const ata_journal_transport_t host_journal_transport = {
    .read = host_raw_read,
    .write = host_raw_write,
    .commit_write = host_raw_write,
    .write_deferred = host_raw_write,
    .write_sectors_deferred = host_raw_write_sectors,
    .flush = host_raw_flush,
    .commit_begin = host_commit_begin,
    .commit_write_deferred = host_raw_write,
    .commit_end = host_commit_end,
};

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer,
                     bool is_master) {
    return use_production_journal
        ? ata_undo_journal_read_sector(&host_journal, base, lba, buffer,
                                       is_master)
        : host_disk_read(base, lba, buffer, is_master);
}

bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer,
                      bool is_master) {
    return use_production_journal
        ? ata_undo_journal_write_sector(&host_journal, base, lba, buffer,
                                        is_master)
        : host_raw_write(NULL, base, lba, buffer, is_master);
}

bool ata_read_sectors(unsigned short base, uint32_t lba, uint32_t count,
                      void *buffer, bool is_master) {
    uint8_t *bytes = buffer;
    if (count == 0U || count > ATA_PIO_MAX_SECTORS) return false;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!ata_read_sector(base, lba + index,
                             bytes + index * SECTOR_SIZE, is_master))
            return false;
    }
    return true;
}

bool ata_write_sectors(unsigned short base, uint32_t lba, uint32_t count,
                       const void *buffer, bool is_master) {
    const uint8_t *bytes = buffer;
    if (count == 0U || count > ATA_PIO_MAX_SECTORS) return false;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!ata_write_sector(base, lba + index,
                              (void *)(bytes + index * SECTOR_SIZE),
                              is_master)) return false;
    }
    uint8_t verify[SECTOR_SIZE];
    for (uint32_t index = 0U; index < count; ++index) {
        if (!ata_read_sector(base, lba + index, verify, is_master) ||
            memcmp(verify, bytes + index * SECTOR_SIZE, SECTOR_SIZE) != 0)
            return false;
    }
    return true;
}

bool ata_journal_attach(unsigned short base, bool is_master,
                        uint32_t partition_lba, uint32_t volume_sectors,
                        uint16_t reserved_sectors) {
    ++journal_attach_count;
    if (!use_production_journal) {
        journal_attached = journal_volume_marked;
        journal_base = base;
        journal_master = is_master;
        journal_partition_lba = partition_lba;
        journal_volume_sectors = volume_sectors;
        return true;
    }
    if (!journal_volume_marked) {
        ata_undo_journal_init(&host_journal, &host_journal_transport, NULL);
        return true;
    }
    return ata_undo_journal_attach(&host_journal, base, is_master,
        partition_lba, volume_sectors, reserved_sectors);
}

bool ata_journal_is_attached(unsigned short base, bool is_master,
                             uint32_t partition_lba,
                             uint32_t volume_sectors) {
    if (!use_production_journal)
        return journal_attached && base == journal_base &&
            is_master == journal_master &&
            partition_lba == journal_partition_lba &&
            volume_sectors == journal_volume_sectors;
    return ata_undo_journal_is_attached(&host_journal, base, is_master,
        partition_lba, volume_sectors);
}

bool vfs_host_mutation_begin(void) {
    return !use_production_journal ||
           ata_undo_journal_transaction_begin(&host_journal);
}

bool vfs_host_mutation_end(bool commit) {
    return !use_production_journal ||
           ata_undo_journal_transaction_end(&host_journal, commit);
}

void read_date(int* year, int* month, int* day) {
    *year = test_year;
    *month = test_month;
    *day = test_day;
}

void read_time(int* hours, int* minutes, int* seconds) {
    *hours = test_hour;
    *minutes = test_minute;
    *seconds = test_second;
}

static void set_test_clock(int year, int month, int day, int hour,
                           int minute, int second) {
    test_year = year;
    test_month = month;
    test_day = day;
    test_hour = hour;
    test_minute = minute;
    test_second = second;
}

static void make_test_volume(void) {
    memset(test_disk, 0, sizeof(test_disk));
    journal_volume_marked = true;
    journal_attached = false;
    use_production_journal = false;
    fail_any_write_once = false;
    root_directory_lba = ROOT_DIRECTORY_LBA;
    primary_fat_lba = 1U;
    secondary_fat_lba = 2U;
    campaign_power_cut = false;
    campaign_fault_armed = false;
    campaign_cut_after_write = 0U;
    campaign_write_count = 0U;
    campaign_flush_count = 0U;
    ata_undo_journal_init(&host_journal, &host_journal_transport, NULL);
    root_read_failures = 0;
    fail_directory_write_once = false;
    fail_directory_verify_once = false;
    payload_writes_before_failure = -1;
    fat_writes_before_verify_failure = -1;
    fat_verify_read_failures = 0;
    fat_verify_failure_burst = 1;
    fat_sector_reads = 0U;
    set_test_clock(2026, 8, 3, 12, 34, 56);
    struct fat32_boot_sector boot;
    memset(&boot, 0, sizeof(boot));
    boot.bytes_per_sector = SECTOR_SIZE;
    boot.sectors_per_cluster = 1;
    boot.reserved_sector_count = 1;
    boot.number_of_fats = 2;
    boot.total_sectors_32 = TEST_SECTORS;
    boot.fat_size_32 = 1;
    boot.root_cluster = 2;
    boot.fs_info = 0;
    boot.boot_sector_signature = 0xAA55;
    memcpy(test_disk[0], &boot, sizeof(boot));

    uint32_t* fat = (uint32_t*)test_disk[1];
    fat[0] = 0x0FFFFFF8;
    fat[1] = FAT32_EOC_MAX;
    fat[2] = FAT32_EOC_MAX;
    memcpy(test_disk[2], test_disk[1], SECTOR_SIZE);
}

static void make_fsinfo_test_volume(void) {
    make_test_volume();
    memset(test_disk, 0, sizeof(test_disk));
    root_directory_lba = 4U;
    primary_fat_lba = 2U;
    secondary_fat_lba = 3U;

    struct fat32_boot_sector boot;
    memset(&boot, 0, sizeof(boot));
    boot.bytes_per_sector = SECTOR_SIZE;
    boot.sectors_per_cluster = 1U;
    boot.reserved_sector_count = 2U;
    boot.number_of_fats = 2U;
    boot.total_sectors_32 = TEST_SECTORS;
    boot.fat_size_32 = 1U;
    boot.root_cluster = 2U;
    boot.fs_info = 1U;
    boot.boot_sector_signature = 0xAA55U;
    memcpy(test_disk[0], &boot, sizeof(boot));

    struct fat32_fsinfo info;
    memset(&info, 0, sizeof(info));
    info.lead_signature = 0x41615252U;
    info.struct_signature = 0x61417272U;
    info.free_cluster_count =
        (TEST_SECTORS - 4U) - (FSINFO_FIRST_FREE - 2U);
    info.next_free_cluster = FSINFO_FIRST_FREE;
    info.trail_signature = 0xAA550000U;
    memcpy(test_disk[1], &info, sizeof(info));

    uint32_t* fat = (uint32_t*)test_disk[primary_fat_lba];
    fat[0] = 0x0FFFFFF8U;
    fat[1] = FAT32_EOC_MAX;
    for (uint32_t cluster = 2U; cluster < FSINFO_FIRST_FREE; ++cluster) {
        fat[cluster] = FAT32_EOC_MAX;
    }
    memcpy(test_disk[secondary_fat_lba], test_disk[primary_fat_lba],
           SECTOR_SIZE);
}

static unsigned int count_allocated_clusters(void) {
    const uint32_t* fat = (const uint32_t*)test_disk[1];
    const unsigned int total_clusters = TEST_SECTORS - 3u;
    unsigned int allocated = 0;
    for (unsigned int cluster = 2; cluster <= total_clusters + 1u;
         ++cluster) {
        if ((fat[cluster] & 0x0FFFFFFFu) != 0) ++allocated;
    }
    return allocated;
}

static struct fat32_dir_entry* find_raw_root_entry(const char* filename) {
    uint8_t fat_name[11];
    convert_to_83_format(fat_name, filename);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)test_disk[root_directory_lba];
    for (size_t i = 0; i < SECTOR_SIZE / sizeof(*entries); ++i) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] != 0xE5 &&
            (entries[i].attr & 0x08u) == 0 &&
            (entries[i].attr & 0x0Fu) != 0x0Fu &&
            memcmp(entries[i].name, fat_name, sizeof(fat_name)) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

static bool raw_root_contains_utf16_pair(uint16_t high, uint16_t low) {
    const struct fat32_dir_entry* entries =
        (const struct fat32_dir_entry*)test_disk[root_directory_lba];
    for (size_t slot = 0U; slot < SECTOR_SIZE / sizeof(*entries); ++slot) {
        if (entries[slot].name[0] == 0x00U) break;
        if (entries[slot].attr != ATTR_LONG_NAME) continue;
        const struct fat32_lfn_entry* lfn =
            (const struct fat32_lfn_entry*)&entries[slot];
        uint16_t units[FAT32_LFN_CHARS_PER_ENTRY];
        for (uint32_t index = 0U; index < 5U; ++index)
            units[index] = lfn->name1[index];
        for (uint32_t index = 0U; index < 6U; ++index)
            units[5U + index] = lfn->name2[index];
        for (uint32_t index = 0U; index < 2U; ++index)
            units[11U + index] = lfn->name3[index];
        for (uint32_t index = 0U; index + 1U < FAT32_LFN_CHARS_PER_ENTRY;
             ++index)
            if (units[index] == high && units[index + 1U] == low) return true;
    }
    return false;
}

static unsigned int count_raw_root_entries(const char* filename) {
    uint8_t fat_name[11];
    convert_to_83_format(fat_name, filename);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)test_disk[root_directory_lba];
    unsigned int count = 0;
    for (size_t i = 0; i < SECTOR_SIZE / sizeof(*entries); ++i) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] != 0xE5 &&
            (entries[i].attr & 0x08u) == 0 &&
            (entries[i].attr & 0x0Fu) != 0x0Fu &&
            memcmp(entries[i].name, fat_name, sizeof(fat_name)) == 0) {
            ++count;
        }
    }
    return count;
}

#define CAMPAIGN_RESERVED_SECTORS 32U
#define CAMPAIGN_FAT1_LBA 32U
#define CAMPAIGN_FAT2_LBA 33U
#define CAMPAIGN_ROOT_LBA 34U

static drive_t campaign_drive;

static void make_campaign_volume(void) {
    memset(test_disk, 0, sizeof(test_disk));
    memset(&campaign_drive, 0, sizeof(campaign_drive));
    root_directory_lba = CAMPAIGN_ROOT_LBA;
    journal_volume_marked = true;
    use_production_journal = true;
    campaign_power_cut = false;
    campaign_fault_armed = false;
    campaign_cut_after_write = 0U;
    campaign_write_count = 0U;
    campaign_flush_count = 0U;
    ata_undo_journal_init(&host_journal, &host_journal_transport, NULL);

    struct fat32_boot_sector boot;
    memset(&boot, 0, sizeof(boot));
    boot.bytes_per_sector = SECTOR_SIZE;
    boot.sectors_per_cluster = 1U;
    boot.reserved_sector_count = CAMPAIGN_RESERVED_SECTORS;
    boot.number_of_fats = 2U;
    boot.total_sectors_32 = TEST_SECTORS;
    boot.fat_size_32 = 1U;
    boot.root_cluster = 2U;
    boot.fs_info = 0U;
    boot.boot_sector_signature = 0xAA55U;
    memcpy(test_disk[0], &boot, sizeof(boot));

    uint32_t *fat = (uint32_t *)test_disk[CAMPAIGN_FAT1_LBA];
    fat[0] = 0x0FFFFFF8U;
    fat[1] = FAT32_EOC_MAX;
    fat[2] = FAT32_EOC_MAX;
    memcpy(test_disk[CAMPAIGN_FAT2_LBA],
           test_disk[CAMPAIGN_FAT1_LBA], SECTOR_SIZE);

    ata_journal_record_t clean;
    ata_undo_journal_make_clean(&clean, 0U);
    memcpy(test_disk[ATA_JOURNAL_HEADER_OFFSET], &clean, sizeof(clean));
    memcpy(test_disk[ATA_JOURNAL_MIRROR_OFFSET], &clean, sizeof(clean));

    campaign_drive.type = DRIVE_TYPE_ATA;
    campaign_drive.base = 0x1F0U;
    campaign_drive.is_master = true;
    campaign_drive.sectors = TEST_SECTORS;
    strcpy(campaign_drive.name, "hdd0");
}

static bool campaign_mount(void) {
    ata_undo_journal_init(&host_journal, &host_journal_transport, NULL);
    vfs_init();
    fat32_register_vfs();
    return vfs_mount(&campaign_drive, "fat32", "/") == VFS_OK;
}

static bool campaign_journal_sector(uint32_t sector) {
    return (sector >= ATA_JOURNAL_HEADER_OFFSET &&
            sector < ATA_JOURNAL_DATA_OFFSET + ATA_JOURNAL_MAX_ENTRIES) ||
           sector == ATA_JOURNAL_MIRROR_OFFSET;
}

static bool campaign_image_uses_known_sectors(void) {
    for (uint32_t sector = 0U; sector < TEST_SECTORS; ++sector) {
        if (campaign_journal_sector(sector)) continue;
        if (memcmp(test_disk[sector], campaign_baseline[sector], SECTOR_SIZE) !=
                0 &&
            memcmp(test_disk[sector], campaign_committed[sector], SECTOR_SIZE) !=
                0) return false;
    }
    return true;
}

static bool campaign_image_matches(
        const uint8_t expected[TEST_SECTORS][SECTOR_SIZE]) {
    for (uint32_t sector = 0U; sector < TEST_SECTORS; ++sector) {
        if (!campaign_journal_sector(sector) &&
            memcmp(test_disk[sector], expected[sector], SECTOR_SIZE) != 0)
            return false;
    }
    return true;
}

static bool campaign_verify_mounted_state(bool committed) {
    if (memcmp(test_disk[CAMPAIGN_FAT1_LBA],
               test_disk[CAMPAIGN_FAT2_LBA], SECTOR_SIZE) != 0) return false;

    vfs_node_t *cut = NULL;
    if (vfs_open("/CUT.TMP", &cut) != VFS_OK || cut == NULL) return false;
    bool valid = committed ? cut->size == 700U :
                             (cut->size == 0U && cut->inode == 0U);
    uint8_t zeroes[700];
    memset(zeroes, 0xA5, sizeof(zeroes));
    if (valid && committed) {
        valid = cut->inode >= 3U &&
            vfs_read(cut, 0U, sizeof(zeroes), zeroes) ==
                (int)sizeof(zeroes);
        for (uint32_t i = 0U; valid && i < sizeof(zeroes); ++i)
            valid = zeroes[i] == 0U;
        const uint32_t *fat =
            (const uint32_t *)test_disk[CAMPAIGN_FAT1_LBA];
        uint32_t next = fat[cut->inode] & 0x0FFFFFFFU;
        valid = valid && next >= 3U && next < FAT32_EOC_MIN &&
            (fat[next] & 0x0FFFFFFFU) >= FAT32_EOC_MIN;
    }
    if (vfs_close(cut) != VFS_OK) valid = false;

    vfs_node_t *keep = NULL;
    uint8_t expected[700];
    uint8_t observed[700];
    for (uint32_t i = 0U; i < sizeof(expected); ++i)
        expected[i] = (uint8_t)(i * 37U + 11U);
    if (vfs_open("/KEEP.BIN", &keep) != VFS_OK || keep == NULL ||
        keep->size != sizeof(expected) ||
        vfs_read(keep, 0U, sizeof(observed), observed) !=
            (int)sizeof(observed) ||
        memcmp(observed, expected, sizeof(expected)) != 0) valid = false;
    if (keep != NULL && vfs_close(keep) != VFS_OK) valid = false;
    return valid;
}

static int run_fat32_image_fault_campaign(void) {
    make_campaign_volume();
    CHECK(campaign_mount());

    uint8_t keep[700];
    for (uint32_t i = 0U; i < sizeof(keep); ++i)
        keep[i] = (uint8_t)(i * 37U + 11U);
    CHECK(vfs_create("/KEEP.BIN") == VFS_OK);
    vfs_node_t *node = NULL;
    CHECK(vfs_open("/KEEP.BIN", &node) == VFS_OK);
    CHECK(vfs_write(node, 0U, sizeof(keep), keep) == (int)sizeof(keep));
    CHECK(vfs_close(node) == VFS_OK);
    CHECK(vfs_create("/CUT.TMP") == VFS_OK);
    CHECK(vfs_open("/CUT.TMP", &node) == VFS_OK);
    CHECK(node->size == 0U && node->inode == 0U);
    CHECK(vfs_close(node) == VFS_OK);
    memcpy(campaign_baseline, test_disk, sizeof(test_disk));

    CHECK(vfs_open("/CUT.TMP", &node) == VFS_OK);
    campaign_fault_armed = true;
    campaign_cut_after_write = UINT_MAX;
    campaign_write_count = 0U;
    campaign_flush_count = 0U;
    CHECK(vfs_truncate(node, 700U) == VFS_OK);
    campaign_fault_armed = false;
    unsigned int measured_writes = campaign_write_count;
    CHECK(measured_writes > 0U &&
          measured_writes <= FAT32_FAULT_CAMPAIGN_MAX_WRITES);
    CHECK(campaign_flush_count == 4U);
    CHECK(vfs_close(node) == VFS_OK);
    memcpy(campaign_committed, test_disk, sizeof(test_disk));
    CHECK(campaign_verify_mounted_state(true));
    CHECK(vfs_unmount("/") == VFS_OK);

    unsigned int recovered = 0U;
    unsigned int rejected = 0U;
    for (unsigned int cut = 1U; cut <= measured_writes; ++cut) {
        memcpy(test_disk, campaign_baseline, sizeof(test_disk));
        campaign_power_cut = false;
        campaign_fault_armed = false;
        campaign_write_count = 0U;
        campaign_flush_count = 0U;
        CHECK(campaign_mount());
        CHECK(vfs_open("/CUT.TMP", &node) == VFS_OK);
        campaign_cut_after_write = cut;
        campaign_fault_armed = true;
        (void)vfs_truncate(node, 700U);
        campaign_fault_armed = false;
        CHECK(campaign_power_cut && campaign_write_count == cut);
        CHECK(campaign_image_uses_known_sectors());

        campaign_power_cut = false;
        CHECK(vfs_close(node) == VFS_OK);
        CHECK(vfs_unmount("/") == VFS_OK);
        if (!campaign_mount()) {
            ++rejected;
            continue;
        }
        bool old_image = campaign_image_matches(campaign_baseline);
        bool new_image = campaign_image_matches(campaign_committed);
        CHECK(old_image != new_image);
        CHECK(campaign_verify_mounted_state(new_image));
        ++recovered;
        CHECK(vfs_unmount("/") == VFS_OK);
    }
    CHECK(recovered + rejected == measured_writes);
    CHECK(recovered != 0U);
    return 0;
}

static bool campaign_verify_lfn_replace(bool committed) {
    static const char source[] = "/source long name.txt";
    static const char target[] = "/target long name.txt";
    static const uint8_t source_data[] = "source-data";
    static const uint8_t target_data[] = "target-data";
    vfs_dir_entry_t info;
    int source_status = vfs_stat(source, &info);
    if ((committed && source_status != VFS_ERR_NOT_FOUND) ||
        (!committed && (source_status != VFS_OK ||
                        info.size != sizeof(source_data) - 1U))) return false;
    vfs_node_t *node = NULL;
    uint8_t observed[sizeof(source_data) - 1U];
    const uint8_t *expected = committed ? source_data : target_data;
    bool valid = vfs_open(target, &node) == VFS_OK && node != NULL &&
        node->size == sizeof(source_data) - 1U &&
        vfs_read(node, 0U, sizeof(observed), observed) ==
            (int)sizeof(observed) &&
        memcmp(observed, expected, sizeof(observed)) == 0;
    if (node != NULL && vfs_close(node) != VFS_OK) valid = false;
    return valid;
}

static int run_fat32_lfn_replace_fault_campaign(void) {
    static const char source[] = "/source long name.txt";
    static const char target[] = "/target long name.txt";
    static const uint8_t source_data[] = "source-data";
    static const uint8_t target_data[] = "target-data";
    make_campaign_volume();
    CHECK(campaign_mount());
    CHECK(vfs_create(source) == VFS_OK);
    CHECK(vfs_create(target) == VFS_OK);
    vfs_node_t *node = NULL;
    CHECK(vfs_open(source, &node) == VFS_OK);
    CHECK(vfs_write(node, 0U, sizeof(source_data) - 1U, source_data) ==
          (int)(sizeof(source_data) - 1U));
    CHECK(vfs_close(node) == VFS_OK);
    CHECK(vfs_open(target, &node) == VFS_OK);
    CHECK(vfs_write(node, 0U, sizeof(target_data) - 1U, target_data) ==
          (int)(sizeof(target_data) - 1U));
    CHECK(vfs_close(node) == VFS_OK);
    CHECK(campaign_verify_lfn_replace(false));
    memcpy(campaign_baseline, test_disk, sizeof(test_disk));

    campaign_fault_armed = true;
    campaign_cut_after_write = UINT_MAX;
    campaign_write_count = 0U;
    campaign_flush_count = 0U;
    CHECK(vfs_rename(source, target) == VFS_OK);
    campaign_fault_armed = false;
    unsigned int measured_writes = campaign_write_count;
    CHECK(measured_writes > 0U &&
          measured_writes <= FAT32_FAULT_CAMPAIGN_MAX_WRITES);
    CHECK(campaign_flush_count == 4U);
    memcpy(campaign_committed, test_disk, sizeof(test_disk));
    CHECK(campaign_verify_lfn_replace(true));
    CHECK(vfs_unmount("/") == VFS_OK);

    unsigned int recovered = 0U;
    unsigned int rejected = 0U;
    for (unsigned int cut = 1U; cut <= measured_writes; ++cut) {
        memcpy(test_disk, campaign_baseline, sizeof(test_disk));
        campaign_power_cut = false;
        campaign_fault_armed = false;
        campaign_write_count = 0U;
        campaign_flush_count = 0U;
        CHECK(campaign_mount());
        campaign_cut_after_write = cut;
        campaign_fault_armed = true;
        (void)vfs_rename(source, target);
        campaign_fault_armed = false;
        CHECK(campaign_power_cut && campaign_write_count == cut);
        CHECK(campaign_image_uses_known_sectors());

        campaign_power_cut = false;
        CHECK(vfs_unmount("/") == VFS_OK);
        if (!campaign_mount()) {
            ++rejected;
            continue;
        }
        bool old_image = campaign_image_matches(campaign_baseline);
        bool new_image = campaign_image_matches(campaign_committed);
        CHECK(old_image != new_image);
        CHECK(campaign_verify_lfn_replace(new_image));
        ++recovered;
        CHECK(vfs_unmount("/") == VFS_OK);
    }
    CHECK(recovered + rejected == measured_writes);
    CHECK(recovered != 0U);
    return 0;
}

static int run_sequential_write_cache_test(void) {
    enum { STREAM_CHUNKS = 48U, STREAM_BYTES = STREAM_CHUNKS * SECTOR_SIZE };
    make_fsinfo_test_volume();
    CHECK(fat32_init_fs_at(0x1F0, true, 0U) == SUCCESS);
    CHECK(fsinfo_valid && fsinfo.next_free_cluster == FSINFO_FIRST_FREE);

    drive_t drive;
    memset(&drive, 0, sizeof(drive));
    drive.type = DRIVE_TYPE_ATA;
    drive.base = 0x1F0;
    drive.is_master = true;
    drive.sectors = TEST_SECTORS;
    strcpy(drive.name, "hdd0");

    vfs_init();
    fat32_register_vfs();
    CHECK(vfs_mount(&drive, "fat32", "/") == VFS_OK);
    CHECK(vfs_create("/STREAM.BIN") == VFS_OK);
    vfs_node_t* stream_node = NULL;
    CHECK(vfs_open("/STREAM.BIN", &stream_node) == VFS_OK &&
          stream_node != NULL);

    uint8_t* stream_data = (uint8_t*)malloc(STREAM_BYTES);
    uint8_t* stream_readback = (uint8_t*)malloc(STREAM_BYTES);
    CHECK(stream_data != NULL && stream_readback != NULL);
    for (uint32_t index = 0U; index < STREAM_BYTES; ++index) {
        stream_data[index] = (uint8_t)(index * 17U + 3U);
    }

    /* This is the exact syscall-facing rhythm: one stable 512-byte kernel
     * bounce per VFS transaction.  The occupied prefix makes a restart at
     * cluster 2 exceed the linear ceiling; the valid FSInfo hint must select
     * the first free cluster directly and advance once per allocation. */
    fat_sector_reads = 0U;
    for (uint32_t chunk = 0U; chunk < STREAM_CHUNKS; ++chunk) {
        CHECK(vfs_write(stream_node, chunk * SECTOR_SIZE, SECTOR_SIZE,
                        stream_data + chunk * SECTOR_SIZE) == SECTOR_SIZE);
    }
    CHECK(stream_node->inode == FSINFO_FIRST_FREE);
    CHECK(fat_sector_reads <= STREAM_CHUNKS * 16U);
    /* Consecutive syscall-sized reads must resume from the prior validated
     * cluster instead of walking from the chain start for every offset. */
    fat_sector_reads = 0U;
    for (uint32_t chunk = 0U; chunk < STREAM_CHUNKS; ++chunk) {
        CHECK(vfs_read(stream_node, chunk * SECTOR_SIZE, SECTOR_SIZE,
                       stream_readback + chunk * SECTOR_SIZE) == SECTOR_SIZE);
    }
    CHECK(fat_sector_reads <= STREAM_CHUNKS * 4U);
    CHECK(memcmp(stream_readback, stream_data, STREAM_BYTES) == 0);

    /* Legacy FAT writes use the same volume-generation hook, so a cached VFS
     * cursor cannot survive an out-of-handle mutation. */
    uint8_t legacy_chunk[SECTOR_SIZE];
    uint8_t resumed_chunk[SECTOR_SIZE];
    memset(legacy_chunk, 0x3CU, sizeof(legacy_chunk));
    memset(resumed_chunk, 0xC3U, sizeof(resumed_chunk));
    FILE* legacy = fat32_open_file("STREAM.BIN", "a");
    CHECK(legacy != NULL);
    CHECK(fat32_write_file(legacy, legacy_chunk, sizeof(legacy_chunk),
                           sizeof(legacy_chunk)) == SECTOR_SIZE);
    free(legacy);
    CHECK(vfs_write(stream_node, STREAM_BYTES + SECTOR_SIZE, SECTOR_SIZE,
                    resumed_chunk) == SECTOR_SIZE);
    CHECK(vfs_read(stream_node, STREAM_BYTES, 2U * SECTOR_SIZE,
                   stream_readback) == (int)(2U * SECTOR_SIZE));
    CHECK(memcmp(stream_readback, legacy_chunk, SECTOR_SIZE) == 0);
    CHECK(memcmp(stream_readback + SECTOR_SIZE, resumed_chunk,
                 SECTOR_SIZE) == 0);

    /* A mutation through another open VFS handle must invalidate both hints,
     * even if truncation later reuses a freed cluster. */
    vfs_node_t* stream_peer = NULL;
    CHECK(vfs_open("/STREAM.BIN", &stream_peer) == VFS_OK &&
          stream_peer != NULL);
    CHECK(vfs_truncate(stream_peer, SECTOR_SIZE) == VFS_OK);
    memset(stream_data, 0xA5, SECTOR_SIZE);
    CHECK(vfs_write(stream_peer, 0U, SECTOR_SIZE, stream_data) == SECTOR_SIZE);
    memset(stream_data + SECTOR_SIZE, 0x5A, SECTOR_SIZE);
    CHECK(vfs_write(stream_node, SECTOR_SIZE, SECTOR_SIZE,
                    stream_data + SECTOR_SIZE) == SECTOR_SIZE);
    CHECK(vfs_read(stream_node, 0U, 2U * SECTOR_SIZE, stream_readback) ==
          (int)(2U * SECTOR_SIZE));
    CHECK(memcmp(stream_readback, stream_data, 2U * SECTOR_SIZE) == 0);

    free(stream_readback);
    free(stream_data);
    CHECK(vfs_close(stream_peer) == VFS_OK);
    CHECK(vfs_close(stream_node) == VFS_OK);
    CHECK(vfs_delete("/STREAM.BIN") == VFS_OK);
    CHECK(vfs_unmount("/") == VFS_OK);
    return 0;
}

int main(void) {
    int cache_test = run_sequential_write_cache_test();
    if (cache_test != 0) return cache_test;
    make_test_volume();
    CHECK(fat32_init_fs_at(0x1F0, true, 0) == SUCCESS);

    /* A late BPB validation failure must not partially switch the active
     * device or replace any cached metadata from the current volume. */
    struct fat32_boot_sector saved_boot = boot_sector;
    struct fat32_fsinfo saved_fsinfo = fsinfo;
    bool saved_fsinfo_valid = fsinfo_valid;
    uint32_t saved_directory = current_directory_cluster;
    uint16_t saved_base = ata_base_address;
    bool saved_master = ata_is_master;
    uint32_t saved_partition = partition_lba_offset;
    struct fat32_boot_sector invalid_boot = boot_sector;
    invalid_boot.root_cluster = TEST_SECTORS;
    memcpy(test_disk[0], &invalid_boot, sizeof(invalid_boot));
    CHECK(fat32_init_fs_at(0x170, false, 0) == FAILURE);
    CHECK(memcmp(&boot_sector, &saved_boot, sizeof(saved_boot)) == 0);
    CHECK(memcmp(&fsinfo, &saved_fsinfo, sizeof(saved_fsinfo)) == 0);
    CHECK(fsinfo_valid == saved_fsinfo_valid);
    CHECK(current_directory_cluster == saved_directory);
    CHECK(ata_base_address == saved_base);
    CHECK(ata_is_master == saved_master);
    CHECK(partition_lba_offset == saved_partition);
    memcpy(test_disk[0], &saved_boot, sizeof(saved_boot));

    CHECK(fat32_is_valid_short_name("HELLO.BAS"));
    CHECK(fat32_is_valid_short_name("A_B.C"));
    CHECK(!fat32_is_valid_short_name("TOO-LONG9.BAS"));
    CHECK(fat32_is_valid_name("long system component.txt"));
    CHECK(!fat32_is_valid_name("invalid/name.txt"));
    CHECK(!fat32_is_valid_short_name("BAD..BAS"));
    unsigned char dot_name[11];
    convert_to_83_format(dot_name, "..");
    CHECK(compare_names((const char*)dot_name, "..") == 0);

    CHECK(fat32_create_file("OLD.BAS"));
    CHECK(fat32_create_file("long system component.txt"));
    struct fat32_dir_entry long_entry;
    char resolved_long_name[MAX_PATH_LENGTH];
    CHECK(fat32_lookup_entry_named(boot_sector.root_cluster,
                                   "long system component.txt", &long_entry,
                                   resolved_long_name) == FAT32_LOOKUP_FOUND);
    CHECK(strcmp(resolved_long_name, "long system component.txt") == 0);
    CHECK(fat32_lookup_entry_in_directory(boot_sector.root_cluster,
                                          "LONGSY~1.TXT", NULL) ==
          FAT32_LOOKUP_FOUND);
    FILE* long_handle = fat32_open_file("long system component.txt", "w");
    CHECK(long_handle != NULL);
    static const char long_payload[] = "lfn-data";
    CHECK(fat32_write_file(long_handle, long_payload,
                           sizeof(long_payload) - 1,
                           sizeof(long_payload) - 1) ==
          (int)sizeof(long_payload) - 1);
    free(long_handle);
    char long_loaded[sizeof(long_payload) - 1];
    CHECK(fat32_load_file_sized("long system component.txt", long_loaded,
                                sizeof(long_loaded)) ==
          (int)sizeof(long_loaded));
    CHECK(memcmp(long_loaded, long_payload, sizeof(long_loaded)) == 0);
    CHECK(fat32_delete_file("long system component.txt"));
    FILE* file = fat32_open_file("OLD.BAS", "w");
    CHECK(file != NULL);
    static const char initial[] = "ABCDEFGHIJ";
    CHECK(fat32_write_file(file, initial, sizeof(initial) - 1,
                           sizeof(initial) - 1) == (int)sizeof(initial) - 1);
    free(file);

    uint32_t size = 0;
    CHECK(fat32_get_file_size("OLD.BAS", &size) == 0 && size == 10);
    char too_small[9];
    CHECK(fat32_load_file_sized("OLD.BAS", too_small,
                                sizeof(too_small)) == -1);
    char loaded[10];
    CHECK(fat32_load_file_sized("OLD.BAS", loaded, sizeof(loaded)) == 10);
    CHECK(memcmp(loaded, initial, sizeof(loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);

    static const char replacement[] = "replacement";
    CHECK(fat32_replace_file("OLD.BAS", replacement,
                             sizeof(replacement) - 1));
    char replacement_loaded[sizeof(replacement) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", replacement_loaded,
                                sizeof(replacement_loaded)) ==
          (int)sizeof(replacement_loaded));
    CHECK(memcmp(replacement_loaded, replacement,
                 sizeof(replacement_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);

    char caller_owned_name[] = "OLD.BAS";
    FILE* refreshed_handle = fat32_open_file(caller_owned_name, "r");
    CHECK(refreshed_handle != NULL);
    caller_owned_name[0] = 'X';
    CHECK(strcmp(refreshed_handle->name, "OLD.BAS") == 0);
    CHECK(fat32_open_file("OLD.BAS", "read") == NULL);

    /* A FAT allocation write which reached disk must survive a transient
     * failure of its immediate verification read without leaking a cluster. */
    static const char fat_retry_commit[] = "FAT-RETRY";
    fat_writes_before_verify_failure = 0;
    CHECK(fat32_replace_file("OLD.BAS", fat_retry_commit,
                             sizeof(fat_retry_commit) - 1));
    char fat_retry_loaded[sizeof(fat_retry_commit) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", fat_retry_loaded,
                                sizeof(fat_retry_loaded)) ==
          (int)sizeof(fat_retry_loaded));
    CHECK(memcmp(fat_retry_loaded, fat_retry_commit,
                 sizeof(fat_retry_loaded)) == 0);
    char refreshed_loaded[sizeof(fat_retry_commit) - 1];
    CHECK(fat32_read_file(refreshed_handle, refreshed_loaded,
                          sizeof(refreshed_loaded),
                          sizeof(refreshed_loaded)) ==
          (int)sizeof(refreshed_loaded));
    CHECK(memcmp(refreshed_loaded, fat_retry_commit,
                 sizeof(refreshed_loaded)) == 0);
    free(refreshed_handle);
    CHECK(count_allocated_clusters() == 2);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* A write command that completed remains authoritative when all immediate
     * verification reads are unavailable; the allocated cluster must not be
     * lost merely because read-back is temporarily impossible. */
    static const char persistent_verify[] = "PERSISTENT-VERIFY";
    fat_verify_failure_burst = 5;
    fat_writes_before_verify_failure = 0;
    CHECK(fat32_replace_file("OLD.BAS", persistent_verify,
                             sizeof(persistent_verify) - 1));
    char persistent_loaded[sizeof(persistent_verify) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", persistent_loaded,
                                sizeof(persistent_loaded)) ==
          (int)sizeof(persistent_loaded));
    CHECK(memcmp(persistent_loaded, persistent_verify,
                 sizeof(persistent_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* A FILE remains bound to the directory in which it was opened.  A
     * same-named file in the current directory must not be touched. */
    CHECK(fat32_create_dir("SUB"));
    FILE* root_handle = fat32_open_file("OLD.BAS", "a");
    CHECK(root_handle != NULL);
    CHECK(fat32_change_directory("SUB"));
    static const char sub_data[] = "SUB-DATA";
    CHECK(fat32_replace_file("OLD.BAS", sub_data, sizeof(sub_data) - 1));
    const char suffix = '!';
    CHECK(fat32_write_file(root_handle, &suffix, 1, 1) == 1);
    free(root_handle);
    char sub_loaded[sizeof(sub_data) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", sub_loaded,
                                sizeof(sub_loaded)) == (int)sizeof(sub_loaded));
    CHECK(memcmp(sub_loaded, sub_data, sizeof(sub_loaded)) == 0);
    CHECK(fat32_delete_file("OLD.BAS"));
    CHECK(fat32_change_directory("/"));
    static const char root_appended[] = "PERSISTENT-VERIFY!";
    char root_loaded[sizeof(root_appended) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", root_loaded,
                                sizeof(root_loaded)) == (int)sizeof(root_loaded));
    CHECK(memcmp(root_loaded, root_appended, sizeof(root_loaded)) == 0);
    CHECK(fat32_delete_dir("SUB"));
    CHECK(count_allocated_clusters() == 2);

    /* A failed first payload write to an empty file must not orphan the
     * newly allocated first cluster. */
    CHECK(fat32_create_file("EMPTY.BAS"));
    FILE* empty_handle = fat32_open_file("EMPTY.BAS", "a");
    CHECK(empty_handle != NULL);
    payload_writes_before_failure = 0;
    const char rejected = 'X';
    CHECK(fat32_write_file(empty_handle, &rejected, 1, 1) == -1);
    free(empty_handle);
    CHECK(fat32_get_file_size("EMPTY.BAS", &size) == 0 && size == 0);
    CHECK(count_allocated_clusters() == 2);
    CHECK(fat32_delete_file("EMPTY.BAS"));

    /* The directory-sector write succeeds but its immediate verification
     * read fails.  Re-reading the entry must recognize the committed swap. */
    static const char verified_commit[] = "COMMITTED";
    fail_directory_verify_once = true;
    CHECK(fat32_replace_file("OLD.BAS", verified_commit,
                             sizeof(verified_commit) - 1));
    char committed_loaded[sizeof(verified_commit) - 1];
    CHECK(fat32_load_file_sized("OLD.BAS", committed_loaded,
                                sizeof(committed_loaded)) ==
          (int)sizeof(committed_loaded));
    CHECK(memcmp(committed_loaded, verified_commit,
                 sizeof(committed_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);

    /* If an append needs another cluster but its directory-size commit is
     * rejected, detach and reclaim only that unpublished suffix. */
    FILE* growth_handle = fat32_open_file("OLD.BAS", "a");
    CHECK(growth_handle != NULL);
    uint8_t growth[600];
    memset(growth, 'G', sizeof(growth));
    fail_directory_write_once = true;
    CHECK(fat32_write_file(growth_handle, growth, sizeof(growth),
                           sizeof(growth)) == -1);
    free(growth_handle);
    CHECK(fat32_get_file_size("OLD.BAS", &size) == 0 &&
          size == sizeof(verified_commit) - 1);
    CHECK(fat32_load_file_sized("OLD.BAS", committed_loaded,
                                sizeof(committed_loaded)) ==
          (int)sizeof(committed_loaded));
    CHECK(memcmp(committed_loaded, verified_commit,
                 sizeof(committed_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* A committed truncation with a failed immediate verify read is still a
     * successful open, and its former chain must be reclaimed. */
    CHECK(fat32_replace_file("TRUNC.BAS", initial, sizeof(initial) - 1));
    CHECK(count_allocated_clusters() == 3);
    fail_directory_verify_once = true;
    FILE* truncated_handle = fat32_open_file("TRUNC.BAS", "w");
    CHECK(truncated_handle != NULL);
    free(truncated_handle);
    CHECK(fat32_get_file_size("TRUNC.BAS", &size) == 0 && size == 0);
    CHECK(count_allocated_clusters() == 2);
    CHECK(fat32_delete_file("TRUNC.BAS"));

    /* A delete whose directory write committed but verify read failed must
     * still reclaim the now-unreachable data chain. */
    CHECK(fat32_replace_file("DELETE.BAS", initial, sizeof(initial) - 1));
    CHECK(count_allocated_clusters() == 3);
    fail_directory_verify_once = true;
    CHECK(fat32_delete_file("DELETE.BAS"));
    CHECK(count_raw_root_entries("DELETE.BAS") == 0);
    CHECK(count_allocated_clusters() == 2);

    /* A rejected metadata write must leave the old file and allocation map
     * untouched. */
    fail_directory_write_once = true;
    CHECK(!fat32_replace_file("OLD.BAS", replacement,
                              sizeof(replacement) - 1));
    CHECK(fat32_load_file_sized("OLD.BAS", committed_loaded,
                                sizeof(committed_loaded)) ==
          (int)sizeof(committed_loaded));
    CHECK(memcmp(committed_loaded, verified_commit,
                 sizeof(committed_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);

    /* Fail the second payload sector after a new two-cluster chain has been
     * linked.  The incomplete chain must be reclaimed in full. */
    uint8_t large_replacement[700];
    memset(large_replacement, 'Q', sizeof(large_replacement));
    payload_writes_before_failure = 1;
    CHECK(!fat32_replace_file("OLD.BAS", large_replacement,
                              sizeof(large_replacement)));
    CHECK(fat32_load_file_sized("OLD.BAS", committed_loaded,
                                sizeof(committed_loaded)) ==
          (int)sizeof(committed_loaded));
    CHECK(memcmp(committed_loaded, verified_commit,
                 sizeof(committed_loaded)) == 0);
    CHECK(count_allocated_clusters() == 2);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* An unreadable directory is an error, not proof that a name is free. */
    root_read_failures = 1;
    CHECK(!fat32_replace_file("IOERR.BAS", replacement,
                              sizeof(replacement) - 1));
    CHECK(count_raw_root_entries("IOERR.BAS") == 0);
    CHECK(count_allocated_clusters() == 2);

    struct fat32_dir_entry* raw_old = find_raw_root_entry("OLD.BAS");
    CHECK(raw_old != NULL);
    raw_old->attr |= ATTR_READ_ONLY;
    CHECK(!fat32_replace_file("OLD.BAS", replacement,
                              sizeof(replacement) - 1));
    CHECK(!fat32_delete_file("OLD.BAS"));
    CHECK(count_raw_root_entries("OLD.BAS") == 1);
    raw_old->attr &= (uint8_t)~ATTR_READ_ONLY;
    CHECK(!fat32_replace_file(".", replacement, sizeof(replacement) - 1));
    CHECK(!fat32_replace_file("..", replacement, sizeof(replacement) - 1));

    CHECK(fat32_replace_file("NEW.BAS", replacement,
                             sizeof(replacement) - 1));
    CHECK(count_raw_root_entries("NEW.BAS") == 1);
    CHECK(fat32_delete_file("NEW.BAS"));
    CHECK(count_raw_root_entries("NEW.BAS") == 0);
    CHECK(count_allocated_clusters() == 2);

    file = fat32_open_file("OLD.BAS", "w");
    CHECK(file != NULL);
    CHECK(fat32_get_file_size("OLD.BAS", &size) == 0 && size == 0);
    file->position = 600;
    const char final_byte = 'Z';
    CHECK(fat32_write_file(file, &final_byte, 1, 1) == 1);
    free(file);
    CHECK(fat32_get_file_size("OLD.BAS", &size) == 0 && size == 601);
    char* sparse = (char*)malloc(601);
    CHECK(sparse != NULL);
    CHECK(fat32_load_file_sized("OLD.BAS", sparse, 601) == 601);
    for (uint32_t i = 0; i < 600; i++) CHECK(sparse[i] == 0);
    CHECK(sparse[600] == 'Z');
    free(sparse);

    file = fat32_open_file("OLD.BAS", "w");
    CHECK(file != NULL);
    free(file);
    CHECK(fat32_get_file_size("OLD.BAS", &size) == 0 && size == 0);

    char name[9];
    /* Empty files consume directory slots but no data clusters.  The 16th
     * insertion extends the root chain; fail only the link's first verify. */
    fat_writes_before_verify_failure = 1;
    for (uint32_t i = 0; i < 17; i++) {
        name[0] = 'F';
        name[1] = (char)('0' + (i / 10));
        name[2] = (char)('0' + (i % 10));
        name[3] = '\0';
        CHECK(fat32_create_file(name));
    }
    struct fat32_dir_entry* last = find_file_in_directory("F16");
    CHECK(last != NULL);
    free(last);
    CHECK(fat32_delete_file("F00"));
    last = find_file_in_directory("F16");
    CHECK(last != NULL);
    free(last);
    const uint32_t* fat = (const uint32_t*)test_disk[1];
    uint32_t root_next = fat[2] & 0x0FFFFFFFu;
    CHECK(root_next >= 2 && root_next < FAT32_EOC_MIN);
    CHECK((fat[root_next] & 0x0FFFFFFFu) >= FAT32_EOC_MIN);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* A volume label may share its raw 11-byte name with a regular file.  It
     * must never be selected as the file's metadata commit target. */
    make_test_volume();
    CHECK(fat32_init_fs_at(0x1F0, true, 0) == SUCCESS);
    struct fat32_dir_entry* root_entries =
        (struct fat32_dir_entry*)test_disk[ROOT_DIRECTORY_LBA];
    create_directory_entry(&root_entries[0], "SYSTEM", 0, 0x08);
    static const char system_data[] = "SYSTEM-DATA";
    CHECK(fat32_replace_file("SYSTEM", system_data,
                             sizeof(system_data) - 1));
    CHECK(root_entries[0].attr == 0x08);
    CHECK(count_raw_root_entries("SYSTEM") == 1);
    char system_loaded[sizeof(system_data) - 1];
    CHECK(fat32_load_file_sized("SYSTEM", system_loaded,
                                sizeof(system_loaded)) ==
          (int)sizeof(system_loaded));
    CHECK(memcmp(system_loaded, system_data, sizeof(system_loaded)) == 0);
    CHECK(memcmp(test_disk[1], test_disk[2], SECTOR_SIZE) == 0);

    /* Shell regression: a file enumerated through VFS must be openable and
     * readable through that same interface, independent of input case. */
    make_test_volume();
    CHECK(fat32_init_fs_at(0x1F0, true, 0) == SUCCESS);
    static const char readme[] = "VFS shell path regression\r\n";
    CHECK(fat32_replace_file("README.TXT", readme, sizeof(readme) - 1));

    drive_t drive;
    memset(&drive, 0, sizeof(drive));
    drive.type = DRIVE_TYPE_ATA;
    drive.base = 0x1F0;
    drive.is_master = true;
    drive.sectors = TEST_SECTORS;
    strcpy(drive.name, "hdd0");

    vfs_init();
    fat32_register_vfs();
    CHECK(vfs_mount(&drive, "fat32", "/") == VFS_OK);

    vfs_dir_entry_t listed;
    CHECK(vfs_readdir("/", 0, &listed) == VFS_OK);
    CHECK(strcmp(listed.name, "README.TXT") == 0);
    CHECK(listed.type == VFS_FILE);
    CHECK(listed.size == sizeof(readme) - 1);

    const char* spellings[] = {"/README.TXT", "/readme.txt"};
    for (size_t spelling = 0;
         spelling < sizeof(spellings) / sizeof(spellings[0]); ++spelling) {
        vfs_node_t* node = NULL;
        CHECK(vfs_open(spellings[spelling], &node) == VFS_OK);
        CHECK(node != NULL && node->type == VFS_FILE);
        CHECK(node->size == sizeof(readme) - 1);
        char contents[sizeof(readme) - 1];
        CHECK(vfs_read(node, 0, sizeof(contents),
                       (uint8_t*)contents) == (int)sizeof(contents));
        CHECK(memcmp(contents, readme, sizeof(contents)) == 0);
        CHECK(vfs_read(node, sizeof(contents), 1,
                       (uint8_t*)contents) == 0);
        vfs_dir_entry_t open_info;
        CHECK(vfs_fstat(node, &open_info) == VFS_OK);
        CHECK(strcmp(open_info.name, "README.TXT") == 0 &&
              open_info.type == VFS_FILE &&
              open_info.size == sizeof(readme) - 1);
        CHECK(vfs_sync(node) == VFS_OK);
        CHECK(vfs_close(node) == VFS_OK);
    }
    CHECK(cache_flushes == 2);

    static const char unicode_path[] =
        "/Gr\xC3\xBCn-\xF0\x9F\x9A\x80.txt";
    static const char unicode_name[] =
        "Gr\xC3\xBCn-\xF0\x9F\x9A\x80.txt";
    static const uint8_t unicode_payload[] = {'U', 'T', 'F', '8'};
    CHECK(vfs_create(unicode_path) == VFS_OK);
    vfs_node_t* unicode_node = NULL;
    CHECK(vfs_open(unicode_path, &unicode_node) == VFS_OK &&
          unicode_node != NULL);
    CHECK(vfs_write(unicode_node, 0U, sizeof(unicode_payload),
                    unicode_payload) == (int)sizeof(unicode_payload));
    uint8_t unicode_read[sizeof(unicode_payload)];
    CHECK(vfs_read(unicode_node, 0U, sizeof(unicode_read), unicode_read) ==
          (int)sizeof(unicode_read));
    CHECK(memcmp(unicode_read, unicode_payload, sizeof(unicode_read)) == 0);
    CHECK(vfs_close(unicode_node) == VFS_OK);
    CHECK(vfs_stat(unicode_path, &listed) == VFS_OK &&
          strcmp(listed.name, unicode_name) == 0 &&
          listed.size == sizeof(unicode_payload));
    bool unicode_listed = false;
    for (uint32_t index = 0U; index < 32U; ++index) {
        int result = vfs_readdir("/", index, &listed);
        if (result == VFS_ERR_NOT_FOUND) break;
        CHECK(result == VFS_OK);
        if (strcmp(listed.name, unicode_name) == 0) unicode_listed = true;
    }
    CHECK(unicode_listed);
    CHECK(raw_root_contains_utf16_pair(0xD83DU, 0xDE80U));
    uint8_t root_before_invalid[SECTOR_SIZE];
    memcpy(root_before_invalid, test_disk[root_directory_lba], SECTOR_SIZE);
    unsigned int allocated_before_invalid = count_allocated_clusters();
    static const char malformed_overlong[] = "/BAD-\xC0\xAF.TXT";
    static const char malformed_surrogate[] = "/BAD-\xED\xA0\x80.TXT";
    static const char malformed_truncated[] = "/BAD-\xF0\x9F\x9A";
    CHECK(vfs_create(malformed_overlong) == VFS_ERR_INVALID);
    CHECK(vfs_create(malformed_surrogate) == VFS_ERR_INVALID);
    CHECK(vfs_create(malformed_truncated) == VFS_ERR_INVALID);
    CHECK(memcmp(root_before_invalid, test_disk[root_directory_lba],
                 SECTOR_SIZE) == 0);
    CHECK(count_allocated_clusters() == allocated_before_invalid);
    CHECK(vfs_delete(unicode_path) == VFS_OK);

    static const char composed_path[] =
        "/\xC3\x84rger-\xC3\x9F.txt";
    static const char composed_name[] =
        "\xC3\x84rger-\xC3\x9F.txt";
    static const char folded_decomposed_path[] =
        "/a\xCC\x88RGER-SS.TXT";
    CHECK(vfs_create(composed_path) == VFS_OK);
    CHECK(vfs_stat(folded_decomposed_path, &listed) == VFS_OK &&
          strcmp(listed.name, composed_name) == 0);
    CHECK(vfs_create(folded_decomposed_path) == VFS_ERR_EXISTS);
    CHECK(vfs_open(folded_decomposed_path, &unicode_node) == VFS_OK &&
          unicode_node != NULL);
    CHECK(vfs_close(unicode_node) == VFS_OK);
    CHECK(vfs_delete(folded_decomposed_path) == VFS_OK);

    static const char unordered_marks[] =
        "/mark-a\xCC\x95\xCC\x80.txt";
    static const char ordered_marks[] =
        "/MARK-A\xCC\x80\xCC\x95.TXT";
    CHECK(vfs_create(unordered_marks) == VFS_OK);
    CHECK(vfs_stat(ordered_marks, &listed) == VFS_OK);
    CHECK(vfs_create(ordered_marks) == VFS_ERR_EXISTS);
    CHECK(vfs_delete(ordered_marks) == VFS_OK);

    /* FAT timestamps are published with the directory entry. Reads and
     * metadata queries never synthesize an implicit access-time write. */
    CHECK(vfs_create("/TIME.TMP") == VFS_OK);
    vfs_dir_entry_t timestamp_info;
    CHECK(vfs_stat("/TIME.TMP", &timestamp_info) == VFS_OK);
    const uint32_t timestamp_created =
        vfs_time_from_calendar(2026, 8, 3, 12, 34, 56);
    const uint32_t timestamp_accessed =
        vfs_time_from_calendar(2026, 8, 3, 0, 0, 0);
    CHECK(timestamp_info.create_time == timestamp_created &&
          timestamp_info.modify_time == timestamp_created &&
          timestamp_info.access_time == timestamp_accessed);
    struct fat32_dir_entry* raw_timestamp = find_raw_root_entry("TIME.TMP");
    CHECK(raw_timestamp != NULL && raw_timestamp->crt_date != 0U &&
          raw_timestamp->crt_time != 0U &&
          raw_timestamp->write_date == raw_timestamp->crt_date &&
          raw_timestamp->write_time == raw_timestamp->crt_time &&
          raw_timestamp->last_access_date == raw_timestamp->crt_date);
    set_test_clock(2026, 8, 3, 12, 36, 59);
    vfs_node_t* timestamp_node = NULL;
    CHECK(vfs_open("/TIME.TMP", &timestamp_node) == VFS_OK);
    const uint8_t timestamp_payload = 'T';
    CHECK(vfs_write(timestamp_node, 0U, 1U, &timestamp_payload) == 1);
    CHECK(vfs_close(timestamp_node) == VFS_OK);
    CHECK(vfs_stat("/TIME.TMP", &timestamp_info) == VFS_OK);
    CHECK(timestamp_info.create_time == timestamp_created &&
          timestamp_info.modify_time ==
              vfs_time_from_calendar(2026, 8, 3, 12, 36, 58) &&
          timestamp_info.access_time == timestamp_accessed);
    struct fat32_dir_entry timestamp_before_read = *raw_timestamp;
    set_test_clock(2026, 8, 4, 1, 2, 3);
    CHECK(vfs_open("/TIME.TMP", &timestamp_node) == VFS_OK);
    uint8_t timestamp_read = 0U;
    CHECK(vfs_read(timestamp_node, 0U, 1U, &timestamp_read) == 1 &&
          timestamp_read == timestamp_payload);
    CHECK(vfs_fstat(timestamp_node, &timestamp_info) == VFS_OK);
    CHECK(vfs_close(timestamp_node) == VFS_OK);
    CHECK(memcmp(&timestamp_before_read, raw_timestamp,
                 sizeof(timestamp_before_read)) == 0);
    CHECK(vfs_touch("/TIME.TMP") == VFS_OK);
    CHECK(vfs_stat("/TIME.TMP", &timestamp_info) == VFS_OK);
    CHECK(timestamp_info.create_time == timestamp_created &&
          timestamp_info.modify_time ==
              vfs_time_from_calendar(2026, 8, 4, 1, 2, 2) &&
          timestamp_info.access_time ==
              vfs_time_from_calendar(2026, 8, 4, 0, 0, 0) &&
          timestamp_info.size == 1U);
    CHECK(vfs_open("/TIME.TMP", &timestamp_node) == VFS_OK);
    const vfs_dir_entry_t timestamp_before_failure = timestamp_info;
    set_test_clock(2026, 8, 4, 1, 4, 5);
    fail_directory_write_once = true;
    CHECK(vfs_truncate(timestamp_node, 0U) == VFS_ERR_IO);
    CHECK(vfs_close(timestamp_node) == VFS_OK);
    CHECK(vfs_stat("/TIME.TMP", &timestamp_info) == VFS_OK);
    CHECK(timestamp_info.size == timestamp_before_failure.size &&
          timestamp_info.create_time == timestamp_before_failure.create_time &&
          timestamp_info.modify_time == timestamp_before_failure.modify_time &&
          timestamp_info.access_time == timestamp_before_failure.access_time);
    CHECK(vfs_delete("/TIME.TMP") == VFS_OK);
    set_test_clock(2026, 8, 3, 12, 34, 56);

    uint8_t truncate_payload[700];
    memset(truncate_payload, 'T', sizeof(truncate_payload));
    unsigned int allocated_before_truncate = count_allocated_clusters();
    CHECK(vfs_create("/TRUNCVFS.TMP") == VFS_OK);
    vfs_node_t* truncate_node = NULL;
    CHECK(vfs_open("/TRUNCVFS.TMP", &truncate_node) == VFS_OK);
    CHECK(vfs_write(truncate_node, 0U, sizeof(truncate_payload),
                    truncate_payload) == (int)sizeof(truncate_payload));
    CHECK(count_allocated_clusters() == allocated_before_truncate + 2U);
    CHECK(vfs_truncate(truncate_node, 200U) == VFS_OK);
    CHECK(truncate_node->size == 200U);
    CHECK(count_allocated_clusters() == allocated_before_truncate + 1U);
    uint8_t truncate_prefix[200];
    CHECK(vfs_read(truncate_node, 0U, sizeof(truncate_prefix),
                   truncate_prefix) == (int)sizeof(truncate_prefix));
    CHECK(memcmp(truncate_prefix, truncate_payload,
                 sizeof(truncate_prefix)) == 0);
    CHECK(vfs_truncate(truncate_node, 700U) == VFS_OK);
    CHECK(truncate_node->size == 700U);
    CHECK(count_allocated_clusters() == allocated_before_truncate + 2U);
    uint8_t truncate_zeroes[500];
    memset(truncate_zeroes, 0xA5, sizeof(truncate_zeroes));
    CHECK(vfs_read(truncate_node, 200U, sizeof(truncate_zeroes),
                   truncate_zeroes) == (int)sizeof(truncate_zeroes));
    for (uint32_t index = 0U; index < sizeof(truncate_zeroes); ++index)
        CHECK(truncate_zeroes[index] == 0U);
    CHECK(vfs_truncate(truncate_node, 700U) == VFS_OK);
    CHECK(vfs_truncate(truncate_node, 0U) == VFS_OK);
    CHECK(truncate_node->inode == 0U && truncate_node->size == 0U);
    CHECK(vfs_fstat(truncate_node, &listed) == VFS_OK &&
          listed.size == 0U && listed.inode == 0U);
    CHECK(count_allocated_clusters() == allocated_before_truncate);
    CHECK(vfs_read(truncate_node, 0U, 1U, truncate_payload) == 0);
    CHECK(vfs_close(truncate_node) == VFS_OK);
    CHECK(vfs_delete("/TRUNCVFS.TMP") == VFS_OK);

    CHECK(vfs_create("/TRFAIL.TMP") == VFS_OK);
    vfs_node_t* failed_truncate = NULL;
    CHECK(vfs_open("/TRFAIL.TMP", &failed_truncate) == VFS_OK);
    CHECK(vfs_write(failed_truncate, 0U, sizeof(truncate_payload),
                    truncate_payload) == (int)sizeof(truncate_payload));
    unsigned int allocated_before_failure = count_allocated_clusters();
    fail_directory_write_once = true;
    CHECK(vfs_truncate(failed_truncate, 200U) == VFS_ERR_IO);
    CHECK(failed_truncate->size == sizeof(truncate_payload));
    CHECK(count_allocated_clusters() == allocated_before_failure);
    uint8_t truncate_verify[sizeof(truncate_payload)];
    CHECK(vfs_read(failed_truncate, 0U, sizeof(truncate_verify),
                   truncate_verify) == (int)sizeof(truncate_verify));
    CHECK(memcmp(truncate_verify, truncate_payload,
                 sizeof(truncate_verify)) == 0);
    CHECK(vfs_close(failed_truncate) == VFS_OK);
    CHECK(vfs_delete("/TRFAIL.TMP") == VFS_OK);

    CHECK(vfs_create("/long vfs filename.txt") == VFS_OK);
    CHECK(vfs_readdir("/", 1, &listed) == VFS_OK);
    CHECK(strcmp(listed.name, "long vfs filename.txt") == 0);
    vfs_node_t* long_node = NULL;
    CHECK(vfs_open("/long vfs filename.txt", &long_node) == VFS_OK);
    CHECK(long_node != NULL && strcmp(long_node->name,
                                     "long vfs filename.txt") == 0);
    CHECK(vfs_close(long_node) == VFS_OK);
    CHECK(vfs_rename("/long vfs filename.txt",
                     "/renamed long vfs filename.txt") == VFS_OK);
    CHECK(vfs_stat("/long vfs filename.txt", &listed) == VFS_ERR_NOT_FOUND);
    CHECK(vfs_stat("/renamed long vfs filename.txt", &listed) == VFS_OK);
    CHECK(strcmp(listed.name, "renamed long vfs filename.txt") == 0);
    CHECK(vfs_delete("/renamed long vfs filename.txt") == VFS_OK);

    static const char lfn_source[] = "/source long replacement name.txt";
    static const char lfn_target[] = "/existing long destination name.txt";
    static const uint8_t lfn_source_data[] = "new long-name payload";
    static const uint8_t lfn_target_data[] = "old payload";
    CHECK(vfs_create(lfn_source) == VFS_OK);
    CHECK(vfs_create(lfn_target) == VFS_OK);
    vfs_node_t* lfn_node = NULL;
    CHECK(vfs_open(lfn_source, &lfn_node) == VFS_OK);
    CHECK(vfs_write(lfn_node, 0U, sizeof(lfn_source_data) - 1U,
                    lfn_source_data) == (int)(sizeof(lfn_source_data) - 1U));
    CHECK(vfs_close(lfn_node) == VFS_OK);
    CHECK(vfs_open(lfn_target, &lfn_node) == VFS_OK);
    CHECK(vfs_write(lfn_node, 0U, sizeof(lfn_target_data) - 1U,
                    lfn_target_data) == (int)(sizeof(lfn_target_data) - 1U));
    CHECK(vfs_close(lfn_node) == VFS_OK);
    unsigned int allocated_before_lfn_replace = count_allocated_clusters();
    CHECK(vfs_rename(lfn_source, lfn_target) == VFS_OK);
    CHECK(vfs_stat(lfn_source, &listed) == VFS_ERR_NOT_FOUND);
    CHECK(vfs_stat(lfn_target, &listed) == VFS_OK &&
          listed.size == sizeof(lfn_source_data) - 1U);
    CHECK(count_allocated_clusters() == allocated_before_lfn_replace - 1U);
    uint8_t lfn_loaded[sizeof(lfn_source_data) - 1U];
    CHECK(vfs_open(lfn_target, &lfn_node) == VFS_OK);
    CHECK(vfs_read(lfn_node, 0U, sizeof(lfn_loaded), lfn_loaded) ==
          (int)sizeof(lfn_loaded));
    CHECK(memcmp(lfn_loaded, lfn_source_data, sizeof(lfn_loaded)) == 0);
    CHECK(vfs_close(lfn_node) == VFS_OK);
    CHECK(vfs_delete(lfn_target) == VFS_OK);

    static const char failed_lfn_source[] = "/failed long source.txt";
    static const char failed_lfn_target[] = "/failed long target.txt";
    CHECK(vfs_create(failed_lfn_source) == VFS_OK);
    CHECK(vfs_create(failed_lfn_target) == VFS_OK);
    fail_any_write_once = true;
    CHECK(vfs_rename(failed_lfn_source, failed_lfn_target) == VFS_ERR_IO);
    CHECK(vfs_stat(failed_lfn_source, &listed) == VFS_OK);
    CHECK(vfs_stat(failed_lfn_target, &listed) == VFS_OK);
    CHECK(vfs_delete(failed_lfn_source) == VFS_OK);
    CHECK(vfs_delete(failed_lfn_target) == VFS_OK);
    CHECK(vfs_create("/lower.txt") == VFS_OK);
    CHECK(vfs_stat("/LOWER.TXT", &listed) == VFS_OK);
    CHECK(strcmp(listed.name, "lower.txt") == 0);
    vfs_node_t* lower_node = NULL;
    CHECK(vfs_open("/LOWER.TXT", &lower_node) == VFS_OK);
    CHECK(vfs_delete("/lower.txt") == VFS_ERR_BUSY);
    CHECK(vfs_rename("/lower.txt", "/LOWER2.TXT") == VFS_ERR_BUSY);
    CHECK(vfs_close(lower_node) == VFS_OK);
    CHECK(vfs_delete("/lower.txt") == VFS_OK);
    CHECK(vfs_mkdir("/long directory name") == VFS_OK);
    CHECK(vfs_create("/long directory name/nested long file.txt") == VFS_OK);
    CHECK(vfs_stat("/long directory name/nested long file.txt", &listed) ==
          VFS_OK);
    CHECK(vfs_delete("/long directory name/nested long file.txt") == VFS_OK);
    CHECK(vfs_rmdir("/long directory name") == VFS_OK);

    /* A maximum-size slot sequence may cross a one-sector directory-cluster
     * boundary; lookup and deletion must retain the complete VFAT chain. */
    char spanning_path[220];
    spanning_path[0] = '/';
    for (size_t i = 1; i <= 200; i++) spanning_path[i] = 'a';
    strcpy(&spanning_path[201], ".txt");
    CHECK(vfs_create(spanning_path) == VFS_OK);
    CHECK(vfs_stat(spanning_path, &listed) == VFS_OK);
    CHECK(strlen(listed.name) == 204);
    CHECK(vfs_delete(spanning_path) == VFS_OK);

    static const char renamed_payload[] = "atomic replacement\r\n";
    CHECK(fat32_replace_file("RST00001.TMP", renamed_payload,
                             sizeof(renamed_payload) - 1));
    CHECK(vfs_rename("/RST00001.TMP", "/README.TXT") == VFS_OK);
    CHECK(vfs_stat("/RST00001.TMP", &listed) == VFS_ERR_NOT_FOUND);
    CHECK(vfs_stat("/README.TXT", &listed) == VFS_OK);
    CHECK(listed.size == sizeof(renamed_payload) - 1);
    vfs_node_t* replaced = NULL;
    CHECK(vfs_open("/README.TXT", &replaced) == VFS_OK);
    char replaced_contents[sizeof(renamed_payload) - 1];
    CHECK(vfs_read(replaced, 0, sizeof(replaced_contents),
                   (uint8_t*)replaced_contents) ==
          (int)sizeof(replaced_contents));
    CHECK(memcmp(replaced_contents, renamed_payload,
                 sizeof(replaced_contents)) == 0);
    CHECK(vfs_close(replaced) == VFS_OK);

    /* Trash storage uses one same-parent directory rename. The child path
     * remains reachable under the new name, while cross-parent rename keeps
     * the architecture's explicit fail-closed boundary. */
    CHECK(vfs_mkdir("/TRASHDIR") == VFS_OK);
    CHECK(vfs_create("/TRASHDIR/CHILD.TXT") == VFS_OK);
    CHECK(vfs_rename("/TRASHDIR", "/RT123456.TRS") == VFS_OK);
    CHECK(vfs_stat("/TRASHDIR", &listed) == VFS_ERR_NOT_FOUND);
    CHECK(vfs_stat("/RT123456.TRS", &listed) == VFS_OK);
    CHECK(listed.type == VFS_DIRECTORY);
    CHECK(vfs_stat("/RT123456.TRS/CHILD.TXT", &listed) == VFS_OK);
    CHECK(vfs_delete("/RT123456.TRS/CHILD.TXT") == VFS_OK);
    CHECK(vfs_rmdir("/RT123456.TRS") == VFS_OK);
    CHECK(vfs_mkdir("/SOURCE") == VFS_OK);
    CHECK(vfs_mkdir("/TARGET") == VFS_OK);
    CHECK(vfs_create("/SOURCE/FILE.TXT") == VFS_OK);
    CHECK(vfs_rename("/SOURCE/FILE.TXT", "/TARGET/FILE.TXT") ==
          VFS_ERR_UNSUPPORTED);
    CHECK(vfs_stat("/SOURCE/FILE.TXT", &listed) == VFS_OK);
    CHECK(vfs_delete("/SOURCE/FILE.TXT") == VFS_OK);
    CHECK(vfs_rmdir("/SOURCE") == VFS_OK);
    CHECK(vfs_rmdir("/TARGET") == VFS_OK);
    CHECK(vfs_mkdir("/KEEPDIR") == VFS_OK);
    CHECK(vfs_create("/DELETE.TXT") == VFS_OK);
    CHECK(vfs_unmount("/") == VFS_OK);

    /* A standards-compatible FAT32 volume without the explicit REIST journal
     * marker remains readable, but every mutating API must fail before the
     * first sector changes. */
    journal_volume_marked = false;
    journal_attached = false;
    uint8_t foreign_snapshot[sizeof(test_disk)];
    memcpy(foreign_snapshot, test_disk, sizeof(test_disk));
    CHECK(vfs_mount(&drive, "fat32", "/") == VFS_OK);
    vfs_node_t* foreign_file = NULL;
    CHECK(vfs_open("/README.TXT", &foreign_file) == VFS_OK);
    CHECK(foreign_file != NULL);
    char foreign_contents[sizeof(renamed_payload) - 1];
    CHECK(vfs_read(foreign_file, 0, sizeof(foreign_contents),
                   (uint8_t*)foreign_contents) ==
          (int)sizeof(foreign_contents));
    CHECK(memcmp(foreign_contents, renamed_payload,
                 sizeof(foreign_contents)) == 0);
    const uint8_t rejected_write = 'X';
    CHECK(vfs_write(foreign_file, 0, 1, &rejected_write) ==
          VFS_ERR_READ_ONLY);
    CHECK(vfs_truncate(foreign_file, 0U) == VFS_ERR_READ_ONLY);
    CHECK(vfs_close(foreign_file) == VFS_OK);
    CHECK(vfs_create("/NEW.TXT") == VFS_ERR_READ_ONLY);
    CHECK(vfs_mkdir("/NEWDIR") == VFS_ERR_READ_ONLY);
    CHECK(vfs_delete("/DELETE.TXT") == VFS_ERR_READ_ONLY);
    CHECK(vfs_rmdir("/KEEPDIR") == VFS_ERR_READ_ONLY);
    CHECK(vfs_rename("/README.TXT", "/RENAMED.TXT") ==
          VFS_ERR_READ_ONLY);
    CHECK(vfs_touch("/README.TXT") == VFS_ERR_READ_ONLY);
    CHECK(fat32_open_file("README.TXT", "w") == NULL);
    CHECK(!fat32_replace_file("README.TXT", replacement,
                              sizeof(replacement) - 1));
    CHECK(memcmp(foreign_snapshot, test_disk, sizeof(test_disk)) == 0);
    CHECK(vfs_unmount("/") == VFS_OK);

    /* Simulate a global ATA journal binding replaced by another mounted
     * volume. The next mutation must reattach this exact volume first. */
    journal_volume_marked = true;
    CHECK(vfs_mount(&drive, "fat32", "/") == VFS_OK);
    journal_attached = true;
    journal_base = 0x170;
    journal_master = false;
    journal_partition_lba = 7;
    journal_volume_sectors = TEST_SECTORS - 7;
    unsigned int attaches_before_rebind = journal_attach_count;
    CHECK(vfs_create("/REBOUND.TXT") == VFS_OK);
    CHECK(journal_attach_count == attaches_before_rebind + 1);
    CHECK(ata_journal_is_attached(drive.base, drive.is_master, 0,
                                  TEST_SECTORS));
    CHECK(vfs_delete("/REBOUND.TXT") == VFS_OK);
    CHECK(vfs_unmount("/") == VFS_OK);
    CHECK(run_fat32_image_fault_campaign() == 0);
    return run_fat32_lfn_replace_fault_campaign();
}
