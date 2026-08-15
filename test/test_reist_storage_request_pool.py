import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReistStorageRequestPoolTests(unittest.TestCase):
    def test_host_pool_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "storage_pool.exe"
            command = [
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", f"-I{ROOT}",
                str(ROOT / "test/test_storage_request_pool_host.c"),
                str(ROOT / "kernel/init/storage_request_pool.c"),
                str(ROOT / "kernel/init/critical_object.c"),
                "-o", str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_pool_is_static_versioned_and_generation_scoped(self):
        header = (ROOT / "include/kernel/storage_request_pool.h").read_text()
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        self.assertIn("STORAGE_REQUEST_POOL_CAPACITY 8U", header)
        self.assertIn("STORAGE_REQUEST_BLOCK_SIZE 512U", header)
        self.assertIn("STORAGE_REQUEST_VERSION 1U", header)
        self.assertIn("STORAGE_REQUEST_VFS_READ", header)
        self.assertIn("STORAGE_REQUEST_VFS_WRITE", header)
        self.assertIn("STORAGE_REQUEST_VFS_SYNC", header)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("k_free", source)
        self.assertIn("client_generation", source)
        self.assertIn("service_generation", source)
        self.assertIn("STORAGE_HANDLE_GENERATION_MAX", source)
        self.assertIn("STORAGE_REQUEST_STATS_VERSION", header)
        self.assertIn("storage_request_stats_t", header)
        self.assertIn("int storage_request_stats(", header)

    def test_capacity_high_water_and_rejections_are_bounded_diagnostics(self):
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        self.assertIn("static storage_request_stats_t request_stats", source)
        self.assertIn("UINT32_MAX", source)
        self.assertIn("request_stats.request_high_water", source)
        self.assertIn("client_capacity_rejections", source)
        self.assertIn("pool_capacity_rejections", source)

    def test_payload_has_crc_and_redundant_copy(self):
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        self.assertIn("storage_data_copy_t primary", source)
        self.assertIn("storage_data_copy_t shadow", source)
        self.assertIn("crc32_bytes", source)
        self.assertIn("if (!primary_valid && !shadow_valid)", source)

    def test_process_exit_cancels_generation_scoped_requests(self):
        process = (ROOT / "kernel/proc/process.c").read_text()
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text()
        self.assertIn("storage_request_cancel_process(process->pid, process->generation)",
                      process)
        self.assertGreaterEqual(scheduler.count("storage_request_cancel_process"), 2)


if __name__ == "__main__":
    unittest.main()
