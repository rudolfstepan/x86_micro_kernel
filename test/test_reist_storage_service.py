import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReistStorageServiceTests(unittest.TestCase):
    def test_service_has_least_privilege_profile(self):
        process = read("kernel/proc/process.c")
        profile = process[process.index("if (kind == PROCESS_DOMAIN_STORAGE)"):
                          process.index("if (kind != PROCESS_DOMAIN_PROBE)")]
        for syscall in ("SYS_STORAGE_BIND", "SYS_STORAGE_CLAIM",
                        "SYS_STORAGE_BLOCK_READ", "SYS_STORAGE_COMPLETE"):
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
        service = read("examples/userspace/storage_service.c")
        self.assertIn("x86os_storage_bind()", service)
        self.assertIn("x86os_storage_claim", service)
        self.assertIn("x86os_storage_block_read", service)
        self.assertIn("x86os_storage_complete", service)
        self.assertIn('"STORAGE.PRG"', read("scripts/build_system_programs.py"))
        self.assertEqual(read("Makefile").count(
            "libexec/reist/storage.prg="), 1)
        self.assertEqual(read("scripts/build-windows.ps1").count(
            "'libexec/reist/storage.prg'"), 1)

    def test_guest_exercises_real_mbr_read(self):
        guest = read("examples/userspace/guest_test.c")
        self.assertIn("TEST_STAGE STORAGE_SERVICE_OK", guest)
        self.assertIn("sector[510] == 0x55U", guest)
        self.assertIn("sector[511] == 0xAAU", guest)


if __name__ == "__main__":
    unittest.main()
