import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistSupervisorTests(unittest.TestCase):
    def test_supervisor_state_machine_and_budgets(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "supervisor-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DREIST_HOST_TEST", "-I", str(ROOT),
                 str(ROOT / "kernel/init/critical_object.c"),
                 str(ROOT / "kernel/init/supervisor.c"),
                 str(ROOT / "test/test_supervisor_host.c"),
                 "-o", str(executable)], check=True, capture_output=True,
                text=True, timeout=30,
            )
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_contract_is_fixed_bounded_fenced_and_ecc_protected(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        self.assertIn("SUPERVISOR_MAX_DOMAINS 8U", header)
        self.assertIn("SUPERVISOR_NAME_CAPACITY 32U", header)
        self.assertIn("supervisor descriptor exceeds protected payload", source)
        self.assertIn("supervisor state exceeds protected payload", source)
        self.assertIn("startup_timeout_ms", header)
        self.assertIn("state.startup_timeout_ms", source)
        self.assertIn("config->startup_timeout_ms != 0U", source)
        self.assertIn("critical_object_t protected_state", source)
        self.assertIn("critical_object_t protected_fence_ops", source)
        self.assertIn("critical_object_t protected_descriptor", source)
        self.assertIn("SUPERVISOR_DESCRIPTOR_VERSION", source)
        self.assertNotIn("bool occupied", source)
        self.assertIn("SUPERVISOR_FENCE_OPS_VERSION", source)
        self.assertIn("fence_ops_read(handle.slot, &fence_ops)", source)
        self.assertIn("SUPERVISOR_EVENT_FENCE_REQUIRED", source)
        self.assertIn("fence_ops.apply(fence_ops.context)", source)
        self.assertIn("fence_ops.verify(fence_ops.context)", source)
        self.assertIn("SUPERVISOR_CHECK_INTERVAL_MS 10U", source)
        self.assertIn("state.state == SUPERVISOR_ISOLATED", source)
        self.assertIn("static uint32_t next_poll_slot", source)
        self.assertIn("(next_poll_slot + offset) % SUPERVISOR_MAX_DOMAINS", source)
        self.assertIn("supervisor_service_one(uint64_t now_ms)", source)
        self.assertIn("KASSERT_NOT_IRQ();", source)
        self.assertIn("KASSERT(irq_enabled());", source)
        service = source[source.index("supervisor_service_one(uint64_t now_ms)"):]
        service = service[:service.index("supervisor_apply_fence(", 1)]
        self.assertNotIn("while (", service)
        self.assertNotIn("supervisor_ack_fenced", header)
        self.assertIn("restart_count >= state.restart_budget", source)
        self.assertIn("state->epoch != handle.epoch", source)
        self.assertIn("if (state.state == SUPERVISOR_IDLE)", source)
        self.assertNotIn("k_malloc", source)
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_init();", kernel)

    def test_network_probe_authority_has_absolute_bounded_deadline(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        host = (ROOT / "test/test_supervisor_host.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_probe_authority_t", header)
        self.assertIn("SUPERVISOR_NETWORK_PROBE_TIMEOUT_MS 250U", source)
        self.assertIn("supervisor_probe_authority_begin", source)
        self.assertIn("supervisor_probe_authority_take", source)
        self.assertIn("supervisor_probe_authority_expire", source)
        self.assertIn("now_ms >= authority->deadline_ms", source)
        self.assertIn("UINT64_MAX - 5U", host)
        self.assertIn("probe_id != UINT32_MAX", host)
        self.assertIn("supervisor_protected_probe_authority_t", header)
        self.assertIn("SUPERVISOR_PROBE_AUTHORITY_VERSION", source)
        self.assertIn("supervisor_test_corrupt_probe_authority", host)
        self.assertIn("SUPERVISOR_EINTEGRITY", host)
        self.assertIn("supervisor_protected_network_context_t", header)
        self.assertIn("SUPERVISOR_NETWORK_CONTEXT_VERSION", source)
        self.assertIn("supervisor_test_corrupt_network_context", host)
        self.assertIn("context_snapshot.gateway != 0x0A000202U", host)
        self.assertIn("supervisor_protected_probe_control_t", header)
        self.assertIn("SUPERVISOR_PROBE_CONTROL_VERSION", source)
        self.assertIn("supervisor_test_corrupt_probe_control", host)
        self.assertIn("transaction_epoch", header)
        self.assertIn("network_epoch", header)
        self.assertIn("supervisor_protected_probe_authority_take_epoch",
                      source)
        self.assertIn("supervisor_protected_network_context_publish_epoch",
                      source)
        self.assertIn("&protected_authority, 301U, 2U", host)
        self.assertIn("&protected_context, 8U, 10U", host)
        self.assertNotIn("bool active;", source[source.index(
            "typedef struct {"):source.index("static supervisor_probe_runtime_t")])

    def test_network_degradation_counters_saturate(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(encoding="utf-8")
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        host = (ROOT / "test/test_supervisor_host.c").read_text(encoding="utf-8")
        for field in ("expired", "queue_fallback", "semantic_reject"):
            self.assertIn(f"uint32_t {field};", header)
        self.assertIn("if (*value != UINT32_MAX)", source)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADED_QUEUE", source)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADED_EXPIRED", source)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADED_SEMANTIC", source)
        self.assertIn("stats.semantic_reject = UINT32_MAX", host)
        self.assertIn("critical_object_t protected_network_degradation_stats",
                      source)
        self.assertIn("SUPERVISOR_NETWORK_DEGRADATION_VERSION", source)
        self.assertIn("supervisor_test_corrupt_network_degradation(false)",
                      host)
        self.assertIn("supervisor_test_corrupt_network_degradation(true)",
                      host)
        self.assertIn("SUPERVISOR_EINTEGRITY", host)

    def test_pit_drives_bounded_supervisor_deadline_checks(self):
        pit = (ROOT / "kernel/time/pit.c").read_text(encoding="utf-8")
        self.assertIn("supervisor_clock_tick(timer_tick_count);", pit)

    def test_reserved_foreground_worker_is_fail_closed(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        scheduler_h = (ROOT / "kernel/sched/scheduler.h").read_text(encoding="utf-8")
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text(encoding="utf-8")
        process_h = (ROOT / "kernel/proc/process.h").read_text(encoding="utf-8")
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        guest = (ROOT / "userspace/programs/guest_test.c").read_text(encoding="utf-8")
        self.assertIn("static void supervisor_worker(void)", source)
        self.assertIn("supervisor_service_one(pit_monotonic_ms())", source)
        self.assertIn("output_fence_all();", source)
        self.assertIn("scheduler_sleep_ms(SUPERVISOR_CHECK_INTERVAL_MS)", source)
        self.assertIn("if (!supervisor_start_worker())", kernel)
        self.assertIn("SUPERVISED_TASK_RESERVE 1U", scheduler_h)
        self.assertIn("available > SUPERVISED_TASK_RESERVE", scheduler)
        self.assertIn("create_supervised_user_task", scheduler)
        self.assertIn("SUPERVISED_PROCESS_RESERVE 1U", process_h)
        self.assertIn("SUPERVISED_RESTART_FRAME_RESERVE 32U", process_h)
        self.assertIn("free_slots <= SUPERVISED_PROCESS_RESERVE", process)
        self.assertIn("memory_stats.free_frame_bytes / PAGE_SIZE", process)
        self.assertIn("process_spawn_supervised", source)
        self.assertIn("GUEST_TEST_TASK_CAPACITY_LIMIT 32U", guest)
        self.assertIn("int children[GUEST_TEST_TASK_CAPACITY_LIMIT];", guest)
        self.assertIn("before.task_capacity - before.active_tasks -", guest)
        self.assertIn("exhausted.active_tasks + exhausted.supervised_reserve !=",
                      guest)
        self.assertIn("int kill_result = x86os_kill(children[index]);", guest)
        self.assertIn("int wait_result = x86os_wait(children[index], &status);",
                      guest)
        self.assertIn("TEST_STAGE REIST_PROGRESS_OK", guest)


if __name__ == "__main__":
    unittest.main()
