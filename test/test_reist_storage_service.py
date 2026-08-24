import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistStorageServiceTests(unittest.TestCase):
    def test_service_has_least_privilege_profile(self):
        process = read("kernel/proc/process.c")
        profile = process[process.index("if (kind == PROCESS_DOMAIN_STORAGE)"):
                          process.index("if (kind == PROCESS_DOMAIN_ADMIN)")]
        for syscall in ("SYS_STORAGE_BIND", "SYS_STORAGE_CLAIM",
                        "SYS_STORAGE_BLOCK_READ", "SYS_STORAGE_COMPLETE",
                        "SYS_BOOT_STATUS", "SYS_STAT"):
            self.assertIn(syscall, profile)
        for forbidden in ("SYS_OPEN", "SYS_WRITE", "SYS_KILL",
                          "SYS_STORAGE_SUBMIT", "SYS_STORAGE_COLLECT"):
            self.assertNotIn(forbidden, profile)

    def test_service_identity_and_restart_are_protected_and_bounded(self):
        source = read("kernel/init/storage_service.c")
        self.assertIn("critical_object_t protected_control", source)
        self.assertIn("STORAGE_SERVICE_RESTART_BUDGET 3U", source)
        self.assertIn("STORAGE_SERVICE_START_TIMEOUT_MS 1000U", source)
        self.assertIn("storage_request_unbind_service", source)
        self.assertIn("storage_fence_writes();", source)
        self.assertIn("filesystem_fence_mutations();", source)

    def test_only_authorized_service_reaches_raw_block_read(self):
        syscalls = read("kernel/syscall/syscall_table.c")
        body = syscalls[syscalls.index("static int syscall_storage_block_read"):
                        syscalls.index("static int syscall_storage_complete")]
        self.assertIn("storage_service_authorized", body)
        self.assertIn("resource >= (uint32_t)drive_count", body)
        self.assertIn("block >= detected_drives[resource].sectors", body)
        self.assertIn("block_device_read_sector", body)
        self.assertIn("DRIVE_TYPE_PARTITION", body)

    def test_service_and_image_packaging_exist(self):
        service = read("userspace/programs/storage_service.c")
        self.assertIn("x86os_storage_bind()", service)
        self.assertIn("x86os_storage_claim", service)
        self.assertIn("x86os_storage_block_read", service)
        self.assertIn("x86os_storage_complete", service)
        self.assertIn("X86OS_STORAGE_VFS_SHADOW_STAT", service)
        self.assertIn("X86OS_VFS_SHADOW_FAT32_STAT", service)
        self.assertIn("X86OS_VFS_SHADOW_FAT_STAT", service)
        self.assertIn("X86OS_VFS_SHADOW_FAT_STAT_AUTHORITY", service)
        self.assertIn("X86OS_VFS_SHADOW_FS_STAT_AUTHORITY", service)
        self.assertIn("vfs_shadow_stat", service)
        self.assertIn("vfs_shadow_read_at", service)
        self.assertIn("vfs_shadow_readdir_at", service)
        self.assertIn("X86OS_VFS_SHADOW_FS_READ_AT", service)
        self.assertIn("X86OS_VFS_SHADOW_FS_READDIR_AT", service)
        self.assertIn("vfs_shadow_fat32.c", read("scripts/build_system_programs.py"))
        self.assertIn('"STORAGE.PRG"', read("scripts/build_system_programs.py"))
        self.assertEqual(read("Makefile").count(
            "libexec/reist/storage.prg="), 1)
        self.assertEqual(read("scripts/build-windows.ps1").count(
            "'libexec/reist/storage.prg'"), 1)

    def test_guest_exercises_real_mbr_read(self):
        guest = read("userspace/programs/guest_test.c")
        self.assertIn("TEST_STAGE STORAGE_SERVICE_OK", guest)
        self.assertIn("sector[510] == 0x55U", guest)
        self.assertIn("sector[511] == 0xAAU", guest)
        self.assertIn("TEST_STAGE STORAGE_VFS_SHADOW_STAT_OK", guest)

    def test_vfs_shadow_frame_is_fixed_validated_and_read_only(self):
        sdk = read("userspace/sdk/include/x86os.h")
        wrapper = read("userspace/sdk/x86os.c")
        service = read("userspace/programs/storage_service.c")
        self.assertIn("X86OS_VFS_SHADOW_PATH_CAPACITY 192U", sdk)
        self.assertIn("x86os_vfs_shadow_frame_t", sdk)
        self.assertIn("sizeof(x86os_vfs_shadow_frame_t) == "
                      "X86OS_STORAGE_BLOCK_SIZE", wrapper)
        self.assertIn("frame->path[0] != '/'", service)
        self.assertIn("frame->reserved[index] != 0U", service)
        self.assertIn("X86OS_SYS_STAT", service)
        self.assertIn("reist_vfs_shadow_fat32_stat", service)
        self.assertIn("reist_vfs_shadow_fat_stat", service)
        self.assertIn("reist_vfs_shadow_ext2_stat", service)
        self.assertNotIn("X86OS_SYS_OPEN", service)

        authority = service[service.index(
            "static int vfs_shadow_authoritative_fat_stat"):
            service.index("static int vfs_shadow_stat")]
        self.assertIn("reist_vfs_shadow_fat_stat", authority)
        self.assertNotIn("X86OS_SYS_STAT", authority)
        self.assertNotIn("x86os_syscall", authority)

        filesystem_authority = service[service.index(
            "static int vfs_shadow_authoritative_filesystem_stat"):
            service.index("static int vfs_shadow_stat")]
        self.assertIn("reist_vfs_shadow_fat_stat", filesystem_authority)
        self.assertIn("reist_vfs_shadow_ext2_stat", filesystem_authority)
        self.assertNotIn("X86OS_SYS_STAT", filesystem_authority)
        self.assertNotIn("x86os_syscall", filesystem_authority)

        guest = read("userspace/programs/guest_test.c")
        self.assertIn("X86OS_VFS_SHADOW_FAT_STAT_AUTHORITY", guest)
        self.assertIn("STORAGE_VFS_FAT_STAT_AUTHORITY_OK", guest)
        self.assertIn("STORAGE_VFS_FS_STAT_AUTHORITY_OK", guest)
        self.assertIn("STORAGE_VFS_READ_CLIENT_OK", guest)
        self.assertIn('"/bin/cat.prg", "/README.TXT"', guest)
        self.assertIn('"/bin/ls.prg", "/"', guest)


if __name__ == "__main__":
    unittest.main()
