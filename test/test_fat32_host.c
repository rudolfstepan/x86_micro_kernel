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
#include <stdlib.h>
#include <string.h>

#define TEST_SECTORS 128u
#define ROOT_DIRECTORY_LBA 3u
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t test_disk[TEST_SECTORS][SECTOR_SIZE];
static unsigned int root_read_failures;
static bool fail_directory_write_once;
static bool fail_directory_verify_once;
static int payload_writes_before_failure = -1;
static int fat_writes_before_verify_failure = -1;
static unsigned int fat_verify_read_failures;
static unsigned int fat_verify_failure_burst = 1;
static unsigned int cache_flushes;
drive_t* current_drive;

bool ata_flush_cache(unsigned short base, bool is_master) {
    (void)base;
    (void)is_master;
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

bool ata_read_sector(unsigned short base, unsigned int lba, void* buffer,
                     bool is_master) {
    (void)base;
    (void)is_master;
    if (lba >= TEST_SECTORS || !buffer) return false;
    if (lba == 1 && fat_verify_read_failures > 0) {
        --fat_verify_read_failures;
        return false;
    }
    if (lba == ROOT_DIRECTORY_LBA && root_read_failures > 0) {
        --root_read_failures;
        return false;
    }
    memcpy(buffer, test_disk[lba], SECTOR_SIZE);
    return true;
}

bool ata_write_sector(unsigned short base, unsigned int lba, void* buffer,
                      bool is_master) {
    (void)base;
    (void)is_master;
    if (lba >= TEST_SECTORS || !buffer) return false;
    if (lba == ROOT_DIRECTORY_LBA && fail_directory_write_once) {
        fail_directory_write_once = false;
        return false;
    }
    if (lba > ROOT_DIRECTORY_LBA && payload_writes_before_failure >= 0 &&
        sector_has_nonzero_data(buffer)) {
        if (payload_writes_before_failure-- == 0) {
            payload_writes_before_failure = -1;
            return false;
        }
    }
    memcpy(test_disk[lba], buffer, SECTOR_SIZE);
    if (lba == 1 && fat_writes_before_verify_failure >= 0) {
        if (fat_writes_before_verify_failure-- == 0) {
            fat_writes_before_verify_failure = -1;
            fat_verify_read_failures = fat_verify_failure_burst;
            fat_verify_failure_burst = 1;
        }
    }
    if (lba == ROOT_DIRECTORY_LBA && fail_directory_verify_once) {
        fail_directory_verify_once = false;
        root_read_failures = 1;
    }
    return true;
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
    (void)base;
    (void)is_master;
    (void)partition_lba;
    (void)volume_sectors;
    (void)reserved_sectors;
    return true;
}

void read_date(int* year, int* month, int* day) {
    *year = 2026;
    *month = 8;
    *day = 3;
}

void read_time(int* hours, int* minutes, int* seconds) {
    *hours = 12;
    *minutes = 34;
    *seconds = 56;
}

static void make_test_volume(void) {
    memset(test_disk, 0, sizeof(test_disk));
    root_read_failures = 0;
    fail_directory_write_once = false;
    fail_directory_verify_once = false;
    payload_writes_before_failure = -1;
    fat_writes_before_verify_failure = -1;
    fat_verify_read_failures = 0;
    fat_verify_failure_burst = 1;
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
        (struct fat32_dir_entry*)test_disk[ROOT_DIRECTORY_LBA];
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

static unsigned int count_raw_root_entries(const char* filename) {
    uint8_t fat_name[11];
    convert_to_83_format(fat_name, filename);
    struct fat32_dir_entry* entries =
        (struct fat32_dir_entry*)test_disk[ROOT_DIRECTORY_LBA];
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

int main(void) {
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
        CHECK(vfs_sync(node) == VFS_OK);
        CHECK(vfs_close(node) == VFS_OK);
    }
    CHECK(cache_flushes == 2);

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
    CHECK(vfs_create("/lower.txt") == VFS_OK);
    CHECK(vfs_stat("/LOWER.TXT", &listed) == VFS_OK);
    CHECK(strcmp(listed.name, "lower.txt") == 0);
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
    CHECK(vfs_unmount("/") == VFS_OK);
    return 0;
}
