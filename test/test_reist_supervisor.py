import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistSupervisorTests(unittest.TestCase):
    def test_driver_diagnostics_bypass_framebuffer_with_bounded_serial_output(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        helper = source[source.index("static void driver_diagnostic_serial"):
                        source.index("int supervisor_device_driver_report")]
        report = source[source.index(
            "if (report->report_type == "
            "DEVICE_DOMAIN_DRIVER_REPORT_DIAGNOSTIC"):
            source.index("} else if (report->report_type ==",
                         source.index(
                             "if (report->report_type == "
                             "DEVICE_DOMAIN_DRIVER_REPORT_DIAGNOSTIC"))]
        self.assertIn("SUPERVISOR_NAME_CAPACITY", helper)
        self.assertIn("name_length + 1U < SUPERVISOR_NAME_CAPACITY", helper)
        self.assertIn("nibble < 8U", helper)
        self.assertIn("serial_write_char", helper)
        self.assertIn("driver_diagnostic_serial(runtime->name, report->value)",
                      report)
        self.assertNotIn("printf(", report)

    def test_compositor_lifecycle_keeps_production_surface_clients_colocated(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        control = source[source.index("static bool compositor_control_valid"):
                         source.index("#endif", source.index(
                             "static bool compositor_control_valid"))]
        self.assertIn("SUPERVISOR_COMPOSITOR_CONTROL_VERSION", source)
        self.assertIn("compositor control exceeds protected payload", source)
        self.assertIn("control->process_generation", control)
        self.assertIn("control->post_ready_cpu_affinity_mask", control)
        spawn = source[source.index("static bool compositor_spawn_next"):
                       source.index("static bool compositor_fence_until")]
        self.assertIn("process_spawn_supervised_prepared", spawn)
        self.assertIn("PROCESS_DOMAIN_COMPOSITOR", spawn)
        self.assertIn("process_start_prepared_supervised", spawn)
        self.assertNotIn("process_set_supervised_affinity", spawn)
        fence = source[source.index("static bool compositor_fence_until"):
                       source.index("static bool compositor_fence_apply")]
        self.assertLess(fence.index("framebuffer_frame_process_cleanup("),
                        fence.index("process_terminate(control.pid)"))
        self.assertNotIn("display_control_deactivate()", fence)
        report_start = source.index(
            "static int compositor_report_if_identity",
            source.index("bool supervisor_compositor_session_active"),
        )
        report = source[report_start:
                        source.index("static void compositor_monitor_process",
                                     report_start)]
        self.assertLess(report.index("REIST_REPORT_SELF_TEST"),
                        report.index("REIST_REPORT_PROGRESS"))
        self.assertLess(report.index("REIST_REPORT_PROGRESS"),
                        report.index("REIST_REPORT_SERVICE_READY"))
        ready = report[report.index("REIST_REPORT_SERVICE_READY"):]
        self.assertLess(ready.index("COMPOSITOR_READY"),
                        ready.index("process_set_supervised_affinity"))
        self.assertNotIn("post_ready_cpu_affinity_mask = 0U", ready)
        self.assertIn("REIST_GUI COMPOSITOR_AP_EXEC cpu=", report)
        self.assertIn("heartbeat_timeout_ms = 2000U", source)
        self.assertIn("recovery_timeout_ms = 1000U", source)
        self.assertIn("restart_budget = 3U", source)
        compositor_start = source[
            source.index("bool supervisor_start_compositor("):
            source.index("bool supervisor_compositor_session_active(")
        ]
        self.assertIn("startup_timeout_ms = 30000U", compositor_start)
        self.assertNotIn("startup_timeout_ms = 30000U", source[
            :source.index("bool supervisor_start_compositor(")])
        self.assertNotIn("supervisor_start_compositor", kernel)
        self.assertNotIn("supervisor_compositor_session_active", kernel)
        self.assertIn("REIST_GUI DESKTOP_AUTOSTART_DISABLED", kernel)
        syscall = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        spawnv = syscall[syscall.index("static int syscall_spawnv"):]
        spawnv = spawnv[:spawnv.index("static int syscall_wait")]
        self.assertIn('strcmp(path, "/usr/gui/bin/desktop.prg") == 0', spawnv)
        self.assertIn("supervisor_start_compositor(", spawnv)
        self.assertIn("pit_monotonic_ms(), 0U, &compositor_pid", spawnv)
        display_connect = source[
            source.index("if (service_id == REIST_SERVICE_DISPLAY_DRIVER)"):
            source.index("static void supervisor_worker(")]
        self.assertIn("PROCESS_DOMAIN_COMPOSITOR", display_connect)
        self.assertIn('strcmp(client->image_path, "/usr/gui/bin/desktop.prg")',
                      display_connect)

    def test_compositor_restart_preserves_desired_mask_and_returns_to_bsp(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        fence = source[source.index("static bool compositor_fence_until"):]
        fence = fence[:fence.index("static bool compositor_fence_apply")]
        self.assertLess(fence.index("TASK_CPU_MASK_BSP"),
                        fence.index("framebuffer_frame_process_cleanup("))
        self.assertLess(fence.index("framebuffer_frame_process_cleanup("),
                        fence.index("process_terminate(control.pid)"))
        self.assertNotIn("display_control_deactivate()", fence)
        self.assertNotIn("post_ready_cpu_affinity_mask = 0U", fence)
        safe = source[source.index("static bool compositor_event"):]
        safe = safe[:safe.index("static void driver_monitor_processes")]
        self.assertIn("control.post_ready_cpu_affinity_mask = 0U", safe)
        progress = source[source.index(
            "static int compositor_report_if_identity",
            source.index("bool supervisor_compositor_session_active")) :]
        progress = progress[:progress.index(
            "static void compositor_monitor_process")]
        self.assertIn("REIST_GUI COMPOSITOR_TIMEOUT_ARMED epoch=%u", progress)
        self.assertIn("compositor_fault_epoch == control.supervisor.epoch",
                      progress)
        self.assertIn("COMPOSITOR_SMP_LIFECYCLE_FAULT_INJECTION ?= 0",
                      makefile)
        self.assertIn("CompositorSmpLifecycleFaultInjection", windows)

    def test_sound_surface_probe_is_compile_time_only(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        self.assertIn("SOUNDPLAYER_SURFACE_PROBE ?= 0", makefile)
        self.assertIn("REIST_SOUNDPLAYER_SURFACE_PROBE", makefile)
        self.assertIn("SoundplayerSurfaceProbe", windows)
        spawn = source[source.index("static bool compositor_spawn_next"):
                       source.index("static bool compositor_fence_apply")]
        self.assertIn("#ifdef REIST_SOUNDPLAYER_SURFACE_PROBE", spawn)
        self.assertIn('"--sound-probe"', spawn)

    def test_driver_control_is_published_before_prepared_task_starts(self):
        source = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        spawn = source[source.index("static bool driver_spawn_next"):
                       source.index("static bool driver_fence_until")]
        self.assertIn("process_spawn_supervised_prepared", spawn)
        self.assertIn("process_set_supervised_affinity", spawn)
        self.assertIn("driver_control_write(runtime, &control)", spawn)
        self.assertIn("process_start_prepared_supervised", spawn)
        self.assertNotIn("process_spawn_supervised_affined", spawn)
        self.assertLess(spawn.index("process_spawn_supervised_prepared"),
                        spawn.index("device_domain_claim"))
        self.assertLess(spawn.index("device_domain_claim"),
                        spawn.index("driver_control_write(runtime, &control)"))
        self.assertLess(spawn.index("driver_control_write(runtime, &control)"),
                        spawn.index("process_start_prepared_supervised"))
        self.assertIn("driver_abort_prepared_spawn", source)

    def test_compositor_vfs_shadow_authority_is_narrow_and_bounded(self):
        process = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8")
        profile = process[
            process.index("if (kind == PROCESS_DOMAIN_COMPOSITOR)"):
            process.index("if (kind != PROCESS_DOMAIN_PROBE)")]
        for syscall in (
            "SYS_STORAGE_SUBMIT",
            "SYS_STORAGE_COLLECT",
            "SYS_STORAGE_CANCEL",
            "SYS_STORAGE_BULK",
        ):
            self.assertIn(syscall, profile)
        submit = syscalls[
            syscalls.index("static int syscall_storage_submit"):
            syscalls.index("static int syscall_storage_claim")]
        self.assertIn(
            "process->domain_profile.kind == PROCESS_DOMAIN_COMPOSITOR",
            submit,
        )
        self.assertIn(
            "request.operation != STORAGE_REQUEST_VFS_SHADOW_STAT", submit
        )
        self.assertIn(
            "request.operation != STORAGE_REQUEST_VFS_BULK_READ", submit
        )
        self.assertLess(
            submit.index("PROCESS_DOMAIN_COMPOSITOR"),
            submit.index("storage_request_submit("),
        )

    def test_network_service_ap_affinity_is_post_ready_and_protected(self):
        header = (ROOT / "include/kernel/supervisor.h").read_text(
            encoding="utf-8")
        source = (ROOT / "kernel/init/supervisor.c").read_text(
            encoding="utf-8")
        kernel = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        self.assertIn("post_ready_cpu_affinity_mask", header)
        self.assertIn("supervisor_set_network_service_current_affinity", source)
        self.assertIn("REIST_NETWORK SERVICE_AP_EXEC cpu=", source)
        fence = source[source.index("static bool probe_fence_apply"):
                       source.index("static void probe_report_recovery_pair")]
        self.assertLess(fence.index("TASK_CPU_MASK_BSP"),
                        fence.index("netstack_revoke_arp_bindings"))
        ready = source[source.index(
            "if (report_type == REIST_REPORT_SERVICE_READY)"):
            source.index("if (report_type == REIST_REPORT_WCET_BASELINE)")]
        self.assertLess(ready.index("REIST_NETWORK SERVICE_READY"),
                        ready.index("process_set_supervised_affinity"))
        self.assertLess(kernel.index("x86_smp_scheduler_probe()"),
                        kernel.index(
                            "supervisor_set_network_service_current_affinity"))

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
        self.assertIn("startup_progress_timeout_ms", header)
        self.assertIn("state->startup_timeout_ms", source)
        self.assertIn("config->startup_timeout_ms != 0U", source)
        self.assertIn("supervisor_report_startup_progress", source)
        self.assertIn("state.startup_deadline_ms", source)
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
        self.assertIn("supervisor_clock_tick(now_ms);", pit)

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
