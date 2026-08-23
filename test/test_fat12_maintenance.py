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
        self.assertIn('"--repair-chains"', source)
        self.assertIn('"--repair-short"', source)
        self.assertIn('"--reclaim-orphans"', source)
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
        self.assertIn("X86OS_STORAGE_CHECK_FAT12 11U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_MIRROR 12U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_CHAINS 13U", sdk)
        self.assertIn("X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES 14U", sdk)
        self.assertIn("X86OS_STORAGE_RECLAIM_FAT12_ORPHANS 15U", sdk)
        self.assertIn("STORAGE_REQUEST_RECLAIM_FAT12_ORPHANS", pool)
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
        self.assertIn("FAT12_MAX_DIRECTORY_REPAIR_SECTORS 64U", service)
        self.assertIn("fat12_cluster_owner[FAT12_CLUSTER_INDEX_CAPACITY]",
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


if __name__ == "__main__":
    unittest.main()
