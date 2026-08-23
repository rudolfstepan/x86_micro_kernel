import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class Fat12MaintenanceContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_block_write_is_append_only_and_storage_authorized(self):
        sdk = self.read("userspace/sdk/include/x86os.h")
        syscall = self.read("kernel/syscall/syscall_table.c")
        process = self.read("kernel/proc/process.c")
        self.assertIn("X86OS_SYS_STORAGE_BLOCK_WRITE = 85", sdk)
        body = syscall[syscall.index("static int syscall_storage_block_write"):
                       syscall.index("static int syscall_storage_complete")]
        self.assertIn("storage_service_authorized", body)
        self.assertIn("DRIVE_TYPE_FDD", body)
        self.assertIn("storage_service_resource_read_only", body)
        self.assertIn("fdc_write_sectors", body)
        self.assertIn("memcmp(data, verify", body)
        self.assertIn("storage_service_report_media_failure(resource, true)", body)
        self.assertIn("SYS_STORAGE_BLOCK_WRITE", process)

    def test_storage_service_mediates_fdd_writes(self):
        service = self.read("userspace/programs/storage_service.c")
        self.assertIn("X86OS_STORAGE_BLOCK_WRITE", service)
        self.assertIn("x86os_storage_block_write", service)

    def test_chkdsk_is_bounded_and_has_no_raw_media_authority(self):
        source = self.read("userspace/programs/chkdsk.c")
        process_h = self.read("kernel/proc/process.h")
        process = self.read("kernel/proc/process.c")
        self.assertIn("MAX_NODES", source)
        self.assertIn("CHKDSK_TIMEOUT_MS 60000U", source)
        self.assertIn('"--repair"', source)
        self.assertIn('"--confirm"', source)
        self.assertIn("X86OS_STORAGE_CHECK_FAT12", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_MIRROR", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CHAINS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES", source)
        self.assertIn("X86OS_STORAGE_RECLAIM_FAT12_ORPHANS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_LOOPS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_LOOPS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_LOOPS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CROSSLINKS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_SIZE", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_VOLUME_LABEL", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_ZERO_FILES", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_ZERO_START_FILES", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DOT_SIZE", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DOT_CLUSTER", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_REQUIRED_CROSSLINKS", source)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_CROSSLINKS", source)
        self.assertIn('"--repair-chains"', source)
        self.assertIn('"--repair-short"', source)
        self.assertIn('"--reclaim-orphans"', source)
        self.assertIn('"--repair-loops"', source)
        self.assertIn('"--repair-dir-loops"', source)
        self.assertIn('"--repair-short-loops"', source)
        self.assertIn('"--repair-crosslinks"', source)
        self.assertIn('"--repair-dir-size"', source)
        self.assertIn('"--repair-volume-label"', source)
        self.assertIn('"--repair-zero-files"', source)
        self.assertIn('"--repair-zero-start"', source)
        self.assertIn('"--repair-dot-size"', source)
        self.assertIn('"--repair-dot-cluster"', source)
        self.assertIn('"--repair-required-crosslinks"', source)
        self.assertIn('"--repair-directory-crosslinks"', source)
        self.assertIn("x86os_storage_submit", source)
        self.assertIn("x86os_storage_collect", source)
        self.assertNotIn("x86os_storage_block_read", source)
        self.assertNotIn("x86os_storage_block_write", source)
        self.assertNotIn("x86os_write(", source)
        self.assertNotIn("x86os_unlink(", source)
        self.assertIn("PROCESS_DOMAIN_MAINTENANCE = 8", process_h)
        profile = process[process.index(
            "if (kind == PROCESS_DOMAIN_MAINTENANCE)"):
            process.index("if (kind == PROCESS_DOMAIN_COMPONENT_ADMIN)")]
        self.assertIn("SYS_STORAGE_SUBMIT, SYS_STORAGE_COLLECT", profile)
        for forbidden in ("SYS_STORAGE_BLOCK_READ", "SYS_STORAGE_BLOCK_WRITE",
                          "SYS_STORAGE_MAINT_ACQUIRE", "SYS_DEVICE_CONTROL"):
            self.assertNotIn(forbidden, profile)

    def test_fat12_requests_are_append_only_and_exactly_authorized(self):
        header = self.read("include/kernel/storage_request_pool.h")
        pool = self.read("kernel/init/storage_request_pool.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        sdk = self.read("userspace/sdk/include/x86os.h")
        self.assertIn("STORAGE_REQUEST_CHECK_FAT12 = 11", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_MIRROR = 12", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_CHAINS = 13", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_SHORT_FILES = 14", header)
        self.assertIn("STORAGE_REQUEST_RECLAIM_FAT12_ORPHANS = 15", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_LOOPS = 16", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_LOOPS = 17",
                      header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_SHORT_LOOPS = 18", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_CROSSLINKS = 19", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_SIZE = 20",
                      header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_VOLUME_LABEL = 21",
                      header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_ZERO_FILES = 22", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_ZERO_START_FILES = 23",
                      header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_DOT_SIZE = 24", header)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_DOT_CLUSTER = 25", header)
        self.assertIn(
            "STORAGE_REQUEST_REPAIR_FAT12_REQUIRED_CROSSLINKS = 26", header)
        self.assertIn(
            "STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_CROSSLINKS = 27", header)
        self.assertIn("X86OS_STORAGE_CHECK_FAT12 11U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_MIRROR 12U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CHAINS 13U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES 14U", sdk)
        self.assertIn("X86OS_STORAGE_RECLAIM_FAT12_ORPHANS 15U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_LOOPS 16U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_LOOPS 17U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_LOOPS 18U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CROSSLINKS 19U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_SIZE 20U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_VOLUME_LABEL 21U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_ZERO_FILES 22U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_ZERO_START_FILES 23U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DOT_SIZE 24U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_DOT_CLUSTER 25U", sdk)
        self.assertIn(
            "X86OS_STORAGE_REPAIR_FAT12_REQUIRED_CROSSLINKS 26U", sdk)
        self.assertIn(
            "X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_CROSSLINKS 27U", sdk)
        self.assertIn("STORAGE_REQUEST_REPAIR_FAT12_DIRECTORY_CROSSLINKS", pool)
        self.assertIn("request.operation >= STORAGE_REQUEST_CHECK_FAT12", syscall)
        self.assertIn("process->domain_profile.kind != "
                      "PROCESS_DOMAIN_MAINTENANCE", syscall)
        self.assertIn("process->domain_profile.kind == "
                      "PROCESS_DOMAIN_MAINTENANCE", syscall)
        self.assertIn("request.operation < STORAGE_REQUEST_CHECK_FAT12", syscall)

    def test_mirror_repair_is_leased_journaled_and_fail_closed(self):
        service = self.read("userspace/programs/storage_service.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        repair = service[service.index("static int fat12_repair_mirror"):
                         service.index("static void format_boot_sector")]
        self.assertIn("fat12_inspect(resource, &layout)", repair)
        self.assertIn("invalid != X86OS_FAT12_RESULT_PRIMARY_INVALID", repair)
        self.assertIn("invalid != X86OS_FAT12_RESULT_SECONDARY_INVALID", repair)
        self.assertIn("x86os_storage_maintenance_acquire(resource, 0U, &token)",
                      repair)
        self.assertGreaterEqual(repair.count(
            "x86os_storage_maintenance_renew(resource, token, 0U)"), 3)
        self.assertGreaterEqual(repair.count("fat12_inspect(resource, &layout)"),
                                3)
        self.assertIn("fat12_load_clean_journal", repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("FAT12_JOURNAL_ACTIVE", repair)
        self.assertIn("FAT12_JOURNAL_CLEAN", repair)
        self.assertIn("x86os_storage_block_flush", repair)
        self.assertIn("x86os_storage_maintenance_release", repair)
        self.assertIn("media_fingerprint != 0U", syscall)
        self.assertIn("media_fingerprint = current_fingerprint", syscall)

    def test_cluster_scan_is_fixed_bounded_and_complete(self):
        service = self.read("userspace/programs/storage_service.c")
        self.assertIn("FAT12_CLUSTER_INDEX_CAPACITY 4086U", service)
        self.assertIn("FAT12_MAX_DIRECTORIES 256U", service)
        self.assertIn("FAT12_MAX_CHAIN_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_SHORT_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_LOOP_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_DIRECTORY_LOOP_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_SHORT_LOOP_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_DIRECTORY_SIZE_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_VOLUME_LABEL_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_ZERO_FILE_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_ZERO_START_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_DOT_SIZE_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_DOT_CLUSTER_REPAIRS 128U", service)
        self.assertIn("FAT12_MAX_REQUIRED_CROSSLINK_FILES 128U", service)
        self.assertIn("FAT12_MAX_EMPTY_DIRECTORY_CROSSLINKS 128U", service)
        self.assertIn("FAT12_MAX_CLONE_CLUSTERS 48U", service)
        self.assertIn("FAT12_MAX_DIRECTORY_REPAIR_SECTORS 64U", service)
        self.assertIn("fat12_cluster_owner[FAT12_CLUSTER_INDEX_CAPACITY]",
                      service)
        self.assertIn("fat12_cluster_references[FAT12_CLUSTER_INDEX_CAPACITY]",
                      service)
        self.assertIn("fat12_cluster_required[FAT12_CLUSTER_INDEX_CAPACITY]",
                      service)
        self.assertIn("fat12_chain_seen[FAT12_CLUSTER_INDEX_CAPACITY]", service)
        scan = service[service.index("static uint32_t fat12_walk_chain"):
                       service.index("static void fat12_copy_bytes")]
        for result in (
            "X86OS_FAT12_RESULT_CHAIN_INVALID",
            "X86OS_FAT12_RESULT_CHAIN_LOOP",
            "X86OS_FAT12_RESULT_CHAIN_CROSSLINK",
            "X86OS_FAT12_RESULT_CHAIN_SHORT",
            "X86OS_FAT12_RESULT_CHAIN_EXCESS",
            "X86OS_FAT12_RESULT_ORPHAN_CLUSTER",
            "X86OS_FAT12_RESULT_DIRECTORY_INVALID",
            "X86OS_FAT12_RESULT_SCAN_LIMIT",
        ):
            self.assertIn(result, scan)
        self.assertIn("steps < state->layout->cluster_count", scan)
        self.assertIn("fat12_scan_root", scan)
        self.assertIn("fat12_scan_subdirectory", scan)
        self.assertNotIn("malloc", scan)

    def test_chain_repair_accepts_only_excess_and_journals_both_fats(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_repair_chains"):
                         service.index("static void format_boot_sector")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_EXCESS",
                      repair)
        self.assertIn("fat12_chain_repair_count == 0U", repair)
        self.assertIn("fat12_apply_chain_repairs", repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("copy < 2U", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("FAT12_JOURNAL_ACTIVE", repair)
        self.assertIn("FAT12_JOURNAL_CLEAN", repair)
        self.assertGreaterEqual(repair.count(
            "x86os_storage_maintenance_renew(resource, token, 0U)"), 3)

    def test_short_file_repair_is_exact_bounded_and_journals_directories(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_repair_short_files"):
                         service.index("static void format_boot_sector")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_SHORT",
                      repair)
        self.assertIn("fat12_short_issue_count != fat12_short_repair_count",
                      repair)
        self.assertIn("fat12_collect_short_repair_sectors", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertIn("fat12_update_short_repair_sector", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("FAT12_JOURNAL_ACTIVE", repair)
        self.assertIn("FAT12_JOURNAL_CLEAN", repair)
        self.assertIn("X86OS_FAT12_RESULT_SHORT_FILES_REPAIRED", repair)
        self.assertGreaterEqual(repair.count(
            "x86os_storage_maintenance_renew(resource, token, 0U)"), 3)

    def test_orphan_reclaim_is_exact_and_preserves_owned_and_bad_clusters(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_apply_orphan_reclaim"):
                         service.index("static int fat12_collect_short_repair_sectors")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_ORPHAN_CLUSTER",
                      repair)
        self.assertIn("fat12_cluster_owner[cluster] != 0U", repair)
        self.assertIn("value == 0x0FF7U", repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0U)", repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("copy < 2U", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_ORPHANS_RECLAIMED", repair)
        self.assertIn("FAT12_JOURNAL_ACTIVE", repair)
        self.assertIn("FAT12_JOURNAL_CLEAN", repair)
        self.assertGreaterEqual(repair.count(
            "x86os_storage_maintenance_renew(resource, token, 0U)"), 3)

    def test_loop_repair_retains_expected_prefix_and_is_fail_closed(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_apply_loop_repairs"):
                         service.index("static int fat12_apply_orphan_reclaim")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_LOOP",
                      repair)
        self.assertIn("fat12_loop_issue_count != fat12_loop_repair_count",
                      repair)
        self.assertIn("index < repair->expected_clusters", repair)
        self.assertIn("fat12_chain_seen[cluster] == fat12_seen_generation",
                      repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU)",
                      repair)
        self.assertIn("steps < layout->cluster_count", repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("copy < 2U", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_LOOPS_REPAIRED", repair)
        self.assertIn("FAT12_JOURNAL_ACTIVE", repair)
        self.assertIn("FAT12_JOURNAL_CLEAN", repair)

    def test_directory_loop_scan_and_repair_preserve_unique_clusters(self):
        service = self.read("userspace/programs/storage_service.c")
        scan = service[service.index("static int fat12_scan_subdirectory"):
                       service.index("static int fat12_scan_chains")]
        self.assertIn("local != X86OS_FAT12_RESULT_CHAIN_LOOP", scan)
        self.assertIn("fat12_chain_seen[cluster] == fat12_seen_generation",
                      scan)
        self.assertIn("fat12_scan_directory_sector", scan)
        repair = service[service.index(
            "static int fat12_apply_directory_loop_repairs"):
            service.index("static int fat12_apply_short_loop_repairs")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_LOOP",
                      repair)
        self.assertIn("fat12_loop_issue_count != "
                      "fat12_directory_loop_repair_count", repair)
        self.assertIn("fat12_chain_seen[next] != fat12_seen_generation",
                      repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU)",
                      repair)
        self.assertNotIn("fat12_set_entry(fat12_repair_fat, cluster, 0U)",
                         repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("copy < 2U", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_DIRECTORY_LOOPS_REPAIRED", repair)

    def test_short_loop_repair_journals_fats_and_directory_before_writes(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_apply_short_loop_repairs"):
                         service.index("static int fat12_collect_directory_size_sectors")]
        self.assertIn("X86OS_FAT12_RESULT_CHAIN_LOOP |", repair)
        self.assertIn("X86OS_FAT12_RESULT_CHAIN_SHORT", repair)
        self.assertIn("fat12_loop_issue_count != "
                      "fat12_short_loop_repair_count", repair)
        self.assertIn("fat12_short_issue_count != "
                      "fat12_short_loop_repair_count", repair)
        self.assertIn("fat12_chain_seen[next] != fat12_seen_generation",
                      repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0x0FFFU)",
                      repair)
        self.assertNotIn("fat12_set_entry(fat12_repair_fat, cluster, 0U)",
                         repair)
        journal_fat = repair.index("fat12_record_old_mirror")
        journal_directory = repair.index("fat12_record_old_sector")
        first_fat_write = repair.index("format_write(resource, target_sector")
        first_directory_write = repair.index(
            "format_write(resource, sector, directory_data)")
        self.assertLess(journal_fat, first_fat_write)
        self.assertLess(journal_directory, first_fat_write)
        self.assertLess(journal_directory, first_directory_write)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_SHORT_LOOPS_REPAIRED", repair)

    def test_crosslink_repair_accepts_only_excess_only_references(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index("static int fat12_crosslinks_are_excess_only"):
                         service.index("static int fat12_apply_loop_repairs")]
        self.assertIn("X86OS_FAT12_RESULT_CHAIN_CROSSLINK |", repair)
        self.assertIn("X86OS_FAT12_RESULT_CHAIN_EXCESS", repair)
        self.assertIn("fat12_excess_issue_count != fat12_chain_repair_count",
                      repair)
        self.assertIn("fat12_cluster_references[cluster] <= 1U", repair)
        self.assertIn("fat12_cluster_required[cluster] > 1U", repair)
        self.assertIn("fat12_cluster_required[cut] != 1U", repair)
        self.assertIn("fat12_cluster_required[cluster] == 0U", repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cut, 0x0FFFU)",
                      repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0U)",
                      repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("copy < 2U", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_CROSSLINKS_REPAIRED", repair)

    def test_required_crosslinks_clone_data_before_atomic_publication(self):
        service = self.read("userspace/programs/storage_service.c")
        repair = service[service.index(
            "static int fat12_required_crosslinks_are_regular"):
            service.index("static int fat12_apply_loop_repairs")]
        self.assertIn(
            "diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_CROSSLINK", repair)
        self.assertIn("fat12_required_crosslink_file_overflow", repair)
        self.assertIn("fat12_regular_references[cluster] !=", repair)
        self.assertIn("fat12_cluster_required[cluster]", repair)
        self.assertIn("fat12_clone_claimed[cluster]", repair)
        self.assertIn("fat12_cluster_references[free_cluster] == 0U", repair)
        self.assertIn("fat12_clone_remaining[cluster] == 0U", repair)
        self.assertIn("FAT12_MAX_CLONE_CLUSTERS", repair)
        self.assertIn("FORMAT_FAT12_JOURNAL_ENTRIES", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertIn("fat12_record_old_mirror", repair)
        self.assertIn("get16(entry + 26U) != file->start_cluster", repair)
        self.assertIn("get32(entry + 28U) != file->original_size", repair)
        self.assertIn("put16(entry + 26U, file->replacement_start)", repair)
        journal_data = repair.index(
            "fat12_record_old_sector(resource, &journal, target")
        journal_fat = repair.index("fat12_record_old_mirror")
        journal_directory = repair.index(
            "fat12_record_old_sector(resource, &journal, sector")
        first_data_write = repair.index(
            "format_write(resource, destination_first + offset")
        first_fat_write = repair.index(
            "format_write(resource, target, replacement)")
        first_directory_write = repair.index(
            "format_write(resource, sector, sector_data)")
        self.assertLess(journal_data, first_data_write)
        self.assertLess(journal_fat, first_data_write)
        self.assertLess(journal_directory, first_data_write)
        self.assertLess(first_data_write, first_fat_write)
        self.assertLess(first_fat_write, first_directory_write)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn(
            "X86OS_FAT12_RESULT_REQUIRED_CROSSLINKS_REPAIRED", repair)

    def test_empty_directory_crosslinks_clone_only_strict_same_parent_leaves(self):
        service = self.read("userspace/programs/storage_service.c")
        scan = service[service.index(
            "static int fat12_empty_directory_entry_valid"):
            service.index("static int fat12_check_volume")]
        self.assertIn("state->layout->sectors_per_cluster != 1U", scan)
        self.assertIn("fat12_is_eoc(fat12_entry", scan)
        self.assertIn("fat12_empty_directory_entry_valid(data, 1U", scan)
        self.assertIn("fat12_empty_directory_entry_valid(data + 32U, 2U", scan)
        self.assertIn("data[64U] != 0U", scan)
        self.assertIn("parent_directory_sector", scan)
        self.assertIn("parent_entry_offset", scan)
        repair = service[service.index(
            "static int fat12_directory_crosslinks_are_empty_same_parent"):
            service.index("static int fat12_apply_loop_repairs")]
        self.assertIn(
            "diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_CROSSLINK", repair)
        self.assertIn("fat12_empty_directory_crosslink_overflow", repair)
        self.assertIn("fat12_regular_references[cluster] !=", repair)
        self.assertIn("fat12_cluster_required[cluster]", repair)
        self.assertIn("fat12_clone_remaining[cluster] != parent_tag", repair)
        self.assertIn("fat12_cluster_references[free_cluster] == 0U", repair)
        self.assertIn("FAT12_MAX_CLONE_CLUSTERS", repair)
        self.assertIn("FORMAT_FAT12_JOURNAL_ENTRIES", repair)
        self.assertIn("put16(data + 26U, destination)", repair)
        self.assertIn("get16(entry + 26U) != directory->start_cluster", repair)
        self.assertIn("put16(entry + 26U, directory->replacement_start)", repair)
        journal_data = repair.index(
            "fat12_record_old_sector(resource, &journal, target")
        journal_fat = repair.index("fat12_record_old_mirror")
        journal_parent = repair.index(
            "fat12_record_old_sector(resource, &journal, sector")
        first_data_write = repair.index(
            "format_write(resource, target, sector_data)")
        first_fat_write = repair.index(
            "format_write(resource, target, replacement)")
        first_parent_write = repair.index(
            "format_write(resource, sector, sector_data)")
        self.assertLess(journal_data, first_data_write)
        self.assertLess(journal_fat, first_data_write)
        self.assertLess(journal_parent, first_data_write)
        self.assertLess(first_data_write, first_fat_write)
        self.assertLess(first_fat_write, first_parent_write)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn(
            "X86OS_FAT12_RESULT_DIRECTORY_CROSSLINKS_REPAIRED", repair)

    def test_directory_size_repair_scans_contents_and_changes_only_size(self):
        service = self.read("userspace/programs/storage_service.c")
        process = service[service.index("static int fat12_process_directory_entry"):
                          service.index("static int fat12_scan_directory_sector")]
        ordinary_directory = process.index("if (start_cluster < 2U")
        size_start = process.index("if (size != 0U)", ordinary_directory)
        size_fault = process[size_start:
                             process.index("return 0;", size_start)]
        self.assertIn("fat12_mark_directory_invalid", size_fault)
        self.assertIn("fat12_add_directory_size_repair", size_fault)
        self.assertIn(
            "fat12_enqueue_directory(state, start_cluster, current_cluster,",
            process)
        repair = service[service.index("static int fat12_collect_directory_size_sectors"):
                         service.index("static int fat12_collect_volume_label_sectors")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID",
                      repair)
        self.assertIn("fat12_directory_invalid_issue_count !=", repair)
        self.assertIn("fat12_directory_size_repair_count", repair)
        self.assertIn("(attributes & 0x18U) != 0x10U", repair)
        self.assertIn("put32(entry + 28U, 0U)", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertNotIn("fat12_record_old_mirror", repair)
        self.assertNotIn("fat12_set_entry", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_DIRECTORY_SIZE_REPAIRED", repair)

    def test_volume_label_repair_changes_only_reserved_entry_fields(self):
        service = self.read("userspace/programs/storage_service.c")
        process = service[service.index("static int fat12_process_directory_entry"):
                          service.index("static int fat12_scan_directory_sector")]
        label = process[process.index("if ((attributes & 0x08U) != 0U)"):
                        process.index("return 0;", process.index(
                            "if ((attributes & 0x08U) != 0U)"))]
        self.assertIn("fat12_mark_directory_invalid", label)
        self.assertIn("fat12_add_volume_label_repair", label)
        repair = service[service.index("static int fat12_collect_volume_label_sectors"):
                         service.index("static int fat12_apply_zero_file_repairs")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID",
                      repair)
        self.assertIn("fat12_directory_invalid_issue_count !=", repair)
        self.assertIn("fat12_volume_label_repair_count", repair)
        self.assertIn("(attributes & 0x18U) != 0x08U", repair)
        self.assertIn("get16(entry + 20U) != 0U", repair)
        self.assertIn("put16(entry + 26U, 0U)", repair)
        self.assertIn("put32(entry + 28U, 0U)", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertNotIn("fat12_record_old_mirror", repair)
        self.assertNotIn("fat12_set_entry", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_VOLUME_LABEL_REPAIRED", repair)

    def test_zero_file_repair_frees_only_unique_chain_and_clears_start(self):
        service = self.read("userspace/programs/storage_service.c")
        scan = service[service.index("static int fat12_process_directory_entry"):
                       service.index("static int fat12_scan_directory_sector")]
        self.assertIn("expected == 0U && start_cluster != 0U", scan)
        self.assertIn("fat12_mark_directory_invalid(state)", scan)
        self.assertIn("fat12_add_zero_file_repair", scan)
        repair = service[service.index("static int fat12_apply_zero_file_repairs"):
                         service.index("static int fat12_collect_zero_start_directory_sectors")]
        self.assertIn("X86OS_FAT12_RESULT_CHAIN_EXCESS |", repair)
        self.assertIn("X86OS_FAT12_RESULT_DIRECTORY_INVALID", repair)
        self.assertIn("fat12_excess_issue_count !=", repair)
        self.assertIn("fat12_directory_invalid_issue_count !=", repair)
        self.assertIn("fat12_cluster_references[cluster] != 1U", repair)
        self.assertIn("fat12_cluster_required[cluster] != 0U", repair)
        self.assertIn("fat12_cluster_owner[cluster] != owner", repair)
        self.assertIn("fat12_is_eoc(next)", repair)
        self.assertIn("fat12_set_entry(fat12_repair_fat, cluster, 0U)", repair)
        self.assertIn("get32(entry + 28U) != 0U", repair)
        self.assertIn("put16(entry + 26U, 0U)", repair)
        journal_fat = repair.index("fat12_record_old_mirror")
        journal_directory = repair.index("fat12_record_old_sector")
        first_fat_write = repair.index("format_write(resource, target_sector")
        first_directory_write = repair.index(
            "format_write(resource, sector, directory_data)")
        self.assertLess(journal_fat, first_fat_write)
        self.assertLess(journal_directory, first_fat_write)
        self.assertLess(journal_directory, first_directory_write)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_ZERO_FILES_REPAIRED", repair)

    def test_zero_start_repair_clears_only_size_without_fat_mutation(self):
        service = self.read("userspace/programs/storage_service.c")
        scan = service[service.index("static int fat12_process_directory_entry"):
                       service.index("static int fat12_scan_directory_sector")]
        self.assertIn("local == X86OS_FAT12_RESULT_CHAIN_SHORT", scan)
        self.assertIn("start_cluster == 0U", scan)
        self.assertIn("actual_clusters == 0U", scan)
        self.assertIn("fat12_add_zero_start_repair", scan)
        repair = service[service.index(
            "static int fat12_collect_zero_start_directory_sectors"):
            service.index("static int fat12_collect_dot_size_sectors")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_CHAIN_SHORT",
                      repair)
        self.assertIn("fat12_short_issue_count !=", repair)
        self.assertIn("fat12_zero_start_repair_count", repair)
        self.assertIn("(attributes & 0x18U) != 0U", repair)
        self.assertIn("get16(entry + 20U) != 0U", repair)
        self.assertIn("get16(entry + 26U) != 0U", repair)
        self.assertIn("get32(entry + 28U) != repair->original_size", repair)
        self.assertIn("put32(entry + 28U, 0U)", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertNotIn("fat12_record_old_mirror", repair)
        self.assertNotIn("fat12_set_entry", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_ZERO_START_FILES_REPAIRED", repair)

    def test_dot_entries_validate_parent_relation_and_repair_only_size(self):
        service = self.read("userspace/programs/storage_service.c")
        self.assertIn("uint16_t parent_cluster;", service)
        scan = service[service.index("static int fat12_dot_entry_kind"):
                       service.index("static int fat12_scan_chains")]
        self.assertIn("index < 11U", scan)
        self.assertIn("entry[index] != ' '", scan)
        self.assertIn("current_cluster, uint32_t parent_cluster", scan)
        self.assertIn("current_cluster == 0U", scan)
        self.assertIn("start_cluster != expected_start", scan)
        self.assertIn("fat12_add_dot_size_repair", scan)
        self.assertIn("fat12_enqueue_directory(state, start_cluster, current_cluster,",
                      scan)
        self.assertIn("first_sector + index, start_cluster, parent_cluster",
                      scan)
        repair = service[service.index("static int fat12_collect_dot_size_sectors"):
                         service.index("static int fat12_collect_dot_cluster_sectors")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID",
                      repair)
        self.assertIn("fat12_directory_invalid_issue_count !=", repair)
        self.assertIn("fat12_dot_size_repair_count", repair)
        self.assertIn("(attributes & 0x18U) != 0x10U", repair)
        self.assertIn("get16(entry + 26U) != repair->expected_start_cluster",
                      repair)
        self.assertIn("dot_kind != repair->dot_kind", repair)
        self.assertIn("put32(entry + 28U, 0U)", repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertNotIn("fat12_record_old_mirror", repair)
        self.assertNotIn("fat12_set_entry", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_DOT_SIZE_REPAIRED", repair)

    def test_dot_cluster_repair_uses_deterministic_parent_and_only_low_word(self):
        service = self.read("userspace/programs/storage_service.c")
        scan = service[service.index("static int fat12_dot_entry_kind"):
                       service.index("static int fat12_scan_directory_sector")]
        mismatch = scan[scan.index("if (start_cluster != expected_start)"):
                        scan.index("return 0;", scan.index(
                            "if (start_cluster != expected_start)"))]
        self.assertIn("fat12_mark_directory_invalid", mismatch)
        self.assertIn("if (size == 0U)", mismatch)
        self.assertIn("fat12_add_dot_cluster_repair", mismatch)
        repair = service[service.index("static int fat12_collect_dot_cluster_sectors"):
                         service.index("static int fat12_apply_orphan_reclaim")]
        self.assertIn("diagnosis != (int)X86OS_FAT12_RESULT_DIRECTORY_INVALID",
                      repair)
        self.assertIn("fat12_directory_invalid_issue_count !=", repair)
        self.assertIn("fat12_dot_cluster_repair_count", repair)
        self.assertIn("(attributes & 0x18U) != 0x10U", repair)
        self.assertIn("get16(entry + 26U) != repair->original_start_cluster",
                      repair)
        self.assertIn("get32(entry + 28U) != 0U", repair)
        self.assertIn("dot_kind != repair->dot_kind", repair)
        self.assertIn("put16(entry + 26U, repair->expected_start_cluster)",
                      repair)
        self.assertIn("fat12_record_old_sector", repair)
        self.assertNotIn("fat12_record_old_mirror", repair)
        self.assertNotIn("fat12_set_entry", repair)
        self.assertIn("fat12_check_volume(resource, &layout) != 0", repair)
        self.assertIn("X86OS_FAT12_RESULT_DOT_CLUSTER_REPAIRED", repair)


if __name__ == "__main__":
    unittest.main()
