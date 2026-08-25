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
        sdk_h = (ROOT / "userspace/sdk/include/x86os.h").read_text()
        self.assertIn("STORAGE_REQUEST_POOL_CAPACITY 8U", header)
        self.assertIn("STORAGE_REQUEST_BLOCK_SIZE 512U", header)
        self.assertIn("STORAGE_REQUEST_VERSION 1U", header)
        self.assertIn("STORAGE_REQUEST_DESCRIPTOR_V2_VERSION 2U", header)
        self.assertIn("STORAGE_REQUEST_VFS_READ", header)
        self.assertIn("STORAGE_REQUEST_VFS_WRITE", header)
        self.assertIn("STORAGE_REQUEST_VFS_SYNC", header)
        self.assertIn("STORAGE_REQUEST_VFS_SHADOW_STAT = 31", header)
        self.assertIn("STORAGE_REQUEST_VFS_BULK_READ = 32", header)
        self.assertIn("STORAGE_REQUEST_BULK_CAPACITY 2U", header)
        self.assertIn("STORAGE_REQUEST_BULK_MAX_BYTES (128U * 1024U)", header)
        self.assertIn("X86OS_STORAGE_BULK_MAX_BYTES (128U * 1024U)", sdk_h)
        self.assertIn("operation_has_input", source)
        self.assertIn("operation_has_output", source)
        self.assertNotIn("k_malloc", source)
        self.assertNotIn("k_free", source)
        self.assertIn("client_generation", source)
        self.assertIn("service_generation", source)
        self.assertIn("STORAGE_HANDLE_GENERATION_MAX", source)
        self.assertIn("STORAGE_REQUEST_STATS_VERSION", header)
        self.assertIn("storage_request_stats_t", header)
        self.assertIn("int storage_request_stats(", header)

    def test_owner_aware_claim_is_append_only_and_keeps_v1_exact(self):
        header = (ROOT / "include/kernel/storage_request_pool.h").read_text()
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        sdk_h = (ROOT / "userspace/sdk/include/x86os.h").read_text()
        sdk_c = (ROOT / "userspace/sdk/x86os.c").read_text()
        self.assertIn("sizeof(storage_request_descriptor_t) == 28U", syscall)
        self.assertIn("sizeof(storage_request_descriptor_v2_t) == 40U",
                      syscall)
        self.assertIn("int storage_request_claim_v2(", header)
        self.assertIn(".client_pid = metadata.client_pid", source)
        self.assertIn(".client_generation = metadata.client_generation",
                      source)
        self.assertIn(".service_generation = metadata.service_generation",
                      source)
        self.assertIn("X86OS_SYS_STORAGE_CLAIM_IDENTITY = 119", sdk_h)
        self.assertIn("sizeof(x86os_storage_descriptor_t) == 28U", sdk_c)
        self.assertIn("sizeof(x86os_storage_descriptor_v2_t) == 40U", sdk_c)
        self.assertIn("(void*)&syscall_storage_claim_identity", syscall)
        self.assertIn("case SYS_STORAGE_CLAIM_IDENTITY", syscall)

    def test_capacity_high_water_and_rejections_are_bounded_diagnostics(self):
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        self.assertIn("static storage_request_stats_t request_stats", source)
        self.assertIn("UINT32_MAX", source)
        self.assertIn("request_stats.request_high_water", source)
        self.assertIn("client_capacity_rejections", source)
        self.assertIn("pool_capacity_rejections", source)

    def test_per_handle_cancel_is_owner_scoped_and_acknowledged(self):
        header = (ROOT / "include/kernel/storage_request_pool.h").read_text()
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        sdk_h = (ROOT / "userspace/sdk/include/x86os.h").read_text()
        sdk_c = (ROOT / "userspace/sdk/x86os.c").read_text()
        self.assertIn("int storage_request_cancel(", header)
        self.assertIn("STORAGE_SLOT_CANCEL_PENDING", source)
        self.assertIn("metadata.client_generation != client_generation", source)
        self.assertIn("return STORAGE_ECANCELED", source)
        self.assertIn("X86OS_SYS_STORAGE_CANCEL = 118", sdk_h)
        self.assertIn("x86os_storage_cancel", sdk_c)
        self.assertIn("(void*)&syscall_storage_cancel", syscall)
        self.assertIn("case SYS_STORAGE_CANCEL", syscall)

    def test_payload_has_crc_and_redundant_copy(self):
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        self.assertIn("storage_data_copy_t primary", source)
        self.assertIn("storage_data_copy_t shadow", source)
        self.assertIn("crc32_bytes", source)
        self.assertIn("if (!primary_valid && !shadow_valid)", source)

    def test_bulk_transfer_is_fixed_crc_checked_and_owner_scoped(self):
        header = (ROOT / "include/kernel/storage_request_pool.h").read_text()
        source = (ROOT / "kernel/init/storage_request_pool.c").read_text()
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        for token in ("storage_request_bulk_publish",
                      "storage_request_bulk_collect"):
            self.assertIn(token, header)
            self.assertIn(token, source)
        self.assertIn("STORAGE_BULK_REVOKED", source)
        self.assertIn("crc32_bytes(bulk->bytes, length)", source)
        self.assertNotIn("k_malloc", source)
        self.assertIn("case SYS_STORAGE_BULK", syscall)
        self.assertIn("scheduler_preempt_disable", syscall)

    def test_process_exit_cancels_generation_scoped_requests(self):
        process = (ROOT / "kernel/proc/process.c").read_text()
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text()
        self.assertIn("storage_request_cancel_process(process->pid, process->generation)",
                      process)
        self.assertGreaterEqual(scheduler.count("storage_request_cancel_process"), 2)


if __name__ == "__main__":
    unittest.main()
