import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AdminMaintenanceContracts(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_admin_abi_is_append_only_and_default_deny(self):
        header = self.read("include/kernel/admin_maintenance.h")
        sdk = self.read("userspace/sdk/include/x86os.h")
        process = self.read("kernel/proc/process.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        self.assertIn("ADMIN_STORAGE_SYSCALL 90U", header)
        self.assertIn("X86OS_SYS_ADMIN_STORAGE = 90", sdk)
        self.assertIn("PROCESS_DOMAIN_ADMIN", process)
        self.assertIn("SYS_ADMIN_STORAGE", process)
        self.assertIn("syscall_admin_storage", syscall)

    def test_root_and_parent_are_rejected_before_transition(self):
        source = self.read("kernel/init/admin_maintenance.c")
        validate = source[
            source.index("static int validate_target"):
            source.index("static int claim_transaction")
        ]
        self.assertIn("protected_root_resource_mask", validate)
        self.assertIn("ADMIN_EROOT", validate)
        self.assertNotIn("storage_service_admin_begin", validate)
        self.assertIn("control.root_resource_mask = discover_root_resource_mask()",
                      source)
        protected = source[
            source.index("static bool protected_root_resource_mask"):
            source.index("bool admin_maintenance_init")
        ]
        self.assertIn("critical_object_read(&protected_control", protected)

    def test_open_drain_is_bounded_and_revokes_after_deadline(self):
        source = self.read("kernel/init/admin_maintenance.c")
        drain = source[
            source.index("static int block_and_drain"):
            source.index("static int flush_resources")
        ]
        self.assertIn("vfs_maintenance_begin", drain)
        self.assertIn("pit_monotonic_ms", drain)
        self.assertIn("scheduler_sleep_ms", drain)
        self.assertIn("process_revoke_files_for_resource", drain)
        self.assertIn("MAX_DRIVES", drain)

    def test_mutation_uses_lease_and_fail_closed_storage_state(self):
        source = self.read("kernel/init/admin_maintenance.c")
        self.assertIn("storage_maintenance_acquire", source)
        self.assertIn("storage_maintenance_valid", source)
        self.assertIn("storage_maintenance_release", source)
        self.assertIn("storage_service_admin_fail", source)
        self.assertIn("storage_service_requalify_media", source)

    def test_vfs_blocks_before_unmount_and_supports_hidden_mount(self):
        header = self.read("fs/vfs/vfs.h")
        source = self.read("fs/vfs/vfs.c")
        self.assertIn("vfs_maintenance_begin", header)
        self.assertIn("vfs_maintenance_open_count", header)
        self.assertIn("vfs_mount_maintenance", header)
        open_body = source[
            source.index("static int vfs_open_locked"):
            source.index("static int vfs_close_locked")
        ]
        self.assertIn("maintenance_blocked", open_body)

    def test_admin_tools_are_built_into_system_images(self):
        programs = self.read("scripts/build_system_programs.py")
        windows = self.read("scripts/build-windows.ps1")
        makefile = self.read("Makefile")
        for name in ("DEVCTL.PRG", "MOUNT.PRG", "UMOUNT.PRG"):
            self.assertIn(name, programs)
            self.assertIn(name, windows)
            self.assertIn(name, makefile)

    def test_admin_tools_are_prevalidated_and_resident_after_root_loss(self):
        process = self.read("kernel/proc/process.c")
        admin = self.read("kernel/init/admin_maintenance.c")
        self.assertIn("RESCUE_PROGRAM_CACHE_CAPACITY", process)
        self.assertIn("critical_object_read(&rescue_program_meta", process)
        self.assertIn("rescue_crc32(cache->image", process)
        self.assertIn("/SHELL.PRG", process)
        self.assertIn("/LS.PRG", process)
        self.assertIn("/CAT.PRG", process)
        self.assertIn("/DEVCTL.PRG", process)
        self.assertIn("/MOUNT.PRG", process)
        self.assertIn("/UMOUNT.PRG", process)
        self.assertIn("load_program_file_uncached", process)
        self.assertIn("process_cache_rescue_programs", admin)


if __name__ == "__main__":
    unittest.main()
