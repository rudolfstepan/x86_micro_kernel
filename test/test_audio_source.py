import hashlib
import shutil
import subprocess
import tempfile
import unittest
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AudioSubsystemTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def compile_and_run(self, output_name: str, sources: list[str],
                        extra: list[str] | None = None) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / output_name
            command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/audio/include"),
                "-I", str(ROOT / "userspace/sdk/include"),
                "-I", str(ROOT / "userspace/drivers/audio"),
            ]
            if extra:
                command.extend(extra)
            command.extend(str(ROOT / source) for source in sources)
            command.extend(["-o", str(executable)])
            subprocess.run(command, check=True, capture_output=True,
                           text=True, timeout=30)
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0)

    def test_hda_parameter_decoding_and_standard_0db_gain(self):
        self.compile_and_run(
            "audio-hda-test.exe",
            ["userspace/drivers/audio/hda_driver.c",
             "userspace/programs/audiotest.c",
             "userspace/audio/lib/audio_wave.c",
             "test/test_audio_host.c"],
            ["-DREIST_HDA_DRIVER_HELPERS_ONLY",
             "-DREIST_AUDIOTEST_HELPERS_ONLY",
             "-DREIST_AUDIO_WAVE_HELPERS_ONLY"],
        )

    def test_downloaded_wave_asset_is_exact_bounded_pcm_fixture(self):
        path = ROOT / "assets/audio/testtone-440hz-mono-48k-s16.wav"
        self.assertEqual(
            hashlib.sha256(path.read_bytes()).hexdigest(),
            "0bd661e92a15bf4d3f14385725c42cc03ee14315dce1fc06a3e735c237799efa",
        )
        with wave.open(str(path), "rb") as fixture:
            self.assertEqual(fixture.getnchannels(), 1)
            self.assertEqual(fixture.getsampwidth(), 2)
            self.assertEqual(fixture.getframerate(), 48000)
            self.assertEqual(fixture.getnframes(), 240000)
            self.assertEqual(fixture.getcomptype(), "NONE")

    def test_public_audio_sdk_behavior(self):
        self.compile_and_run(
            "audio-sdk-test.exe",
            ["userspace/audio/lib/audio.c", "test/test_audio_sdk_host.c"],
        )

    def test_public_abi_is_versioned_fixed_and_pcm_only(self):
        header = self.read("userspace/audio/include/reist/audio.h")
        source = self.read("userspace/audio/lib/audio.c")
        for contract in (
                "REIST_AUDIO_API_VERSION 1U",
                "REIST_AUDIO_PROTOCOL_VERSION 1U",
                "REIST_AUDIO_SAMPLE_RATE 48000U",
                "REIST_AUDIO_CHANNELS 2U",
                "REIST_AUDIO_FORMAT_S16_LE 1U",
                "sizeof(reist_audio_message_t) == 128U"):
            self.assertIn(contract, header)
        self.assertIn("response.request_id != wire->request_id", source)
        self.assertIn("completed != 0U ? (int)completed", source)
        self.assertIn("REIST_AUDIO_CONNECT_ATTEMPTS", source)
        self.assertNotIn("malloc", source)

    def test_driver_and_service_are_separate_default_deny_domains(self):
        process_h = self.read("kernel/proc/process.h")
        process_c = self.read("kernel/proc/process.c")
        supervisor = self.read("kernel/init/supervisor.c")
        self.assertIn("PROCESS_DOMAIN_DRIVER = 6", process_h)
        self.assertIn("PROCESS_DOMAIN_AUDIO_SERVICE = 7", process_h)
        audio_profile = process_c.split(
            "if (kind == PROCESS_DOMAIN_AUDIO_SERVICE)", 1)[1]
        audio_profile = audio_profile.split(
            "if (kind != PROCESS_DOMAIN_PROBE)", 1)[0]
        self.assertIn("SYS_SERVICE_CONNECT", audio_profile)
        for authority in ("SYS_DEVICE_CONTROL", "SYS_DISPLAY_CONTROL",
                          "SYS_NETWORK_CONTROL", "SYS_OPEN"):
            self.assertNotIn(authority, audio_profile)
        self.assertIn("client->domain_profile.kind != "
                      "PROCESS_DOMAIN_AUDIO_SERVICE", supervisor)
        self.assertIn("REIST_SERVICE_AUDIO_DRIVER_INTERNAL", supervisor)
        self.assertIn("client_pid", supervisor)
        self.assertIn("Never delegate an endpoint containing responses",
                      supervisor)
        self.assertIn("audio_service_rotate_session(handle)", supervisor)
        self.assertIn("without consuming the service fault-restart budget",
                      supervisor)
        self.assertIn("supervisor_admin_pause(handle)", supervisor)
        self.assertIn("supervisor_admin_start(handle", supervisor)
        spawn = supervisor.split(
            "static bool audio_service_spawn_next", 1)[1]
        spawn = spawn.split("static bool audio_service_fence_apply", 1)[0]
        self.assertIn("process_spawn_supervised_prepared", spawn)
        self.assertIn("process_start_prepared_supervised", spawn)
        self.assertNotIn("scheduler_preempt_disable();", spawn)
        self.assertNotIn("scheduler_preempt_enable();", spawn)
        scheduler = self.read("kernel/sched/scheduler.c")
        self.assertIn("TASK_PREPARED", scheduler)
        self.assertIn("scheduler_start_prepared_user_task_locked", scheduler)

    def test_vmware_legacy_intx_fallback_is_profile_scoped(self):
        profile = self.read("kernel/init/audio_device_profile.c")
        domain = self.read("kernel/init/device_domain.c")
        header = self.read("include/kernel/device_domain.h")
        self.assertIn("HDA_VMWARE_VENDOR_ID 0x15ADU", profile)
        self.assertIn("HDA_VMWARE_DEVICE_ID 0x1977U", profile)
        self.assertIn("DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC", profile)
        self.assertIn("DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC", header)
        self.assertIn("irq_pic_fallback", domain)
        self.assertIn("hold_pic_line", domain)

    def test_dma_publication_is_kernel_owned_sealed_and_reusable(self):
        domain = self.read("kernel/init/device_domain.c")
        driver = self.read("userspace/drivers/audio/hda_driver.c")
        self.assertIn("dma_pool_storage", domain)
        self.assertIn("DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS",
                      domain)
        self.assertIn("device->state != DEVICE_DOMAIN_DMA_BOUND", domain)
        self.assertIn("device_domain_deactivate", domain)
        self.assertIn("x86os_device_deactivate", driver)
        self.assertIn("stream_abandon", driver)
        self.assertNotIn("HDA_", domain)

    def test_hda_waits_and_resources_are_bounded(self):
        driver = self.read("userspace/drivers/audio/hda_driver.c")
        for bound in ("HDA_RESET_POLLS 100U", "HDA_VERB_POLLS 200U",
                      "HDA_STREAM_POLLS 100U"):
            self.assertIn(bound, driver)
        self.assertIn("reist_hda_amp_0db_gain", driver)
        self.assertIn("reist_hda_amp_playback_gain", driver)
        self.assertIn("HDA_PLAYBACK_BOOST_QUARTER_DB 24U", driver)
        self.assertIn("HDA_PARAMETER_OUTPUT_AMP_CAPS", driver)
        self.assertIn("HDA_WIDGET_CAP_AMP_OVERRIDE", driver)
        self.assertIn("driver->pin_gain_0db", driver)
        self.assertIn("driver->pin_has_output_amp", driver)
        self.assertIn("HDA_CONNECTION_CAPACITY 16U", driver)
        self.assertIn("playback_path_discover", driver)
        self.assertIn("driver->path_has_input_amp", driver)
        self.assertIn("driver->path_connection_index <<", driver)
        self.assertIn("driver->fatal", driver)
        self.assertNotIn("k_malloc", driver)

    def test_healthy_hda_generation_is_ap_affined_after_smp_release(self):
        kernel = self.read("kernel/init/kernel.c")
        supervisor = self.read("kernel/init/supervisor.c")
        release = kernel.split("if (!x86_smp_scheduler_probe())", 1)[1]
        self.assertIn("audio_ap_mask = production_driver_ap_mask", kernel)
        video = kernel.split("if (video_device_available)", 1)[1]
        video = video.split("if (audio_device_available)", 1)[0]
        self.assertNotIn("audio_ap_mask =", video)
        self.assertIn("audio_driver_started && audio_ap_mask != 0U", release)
        self.assertIn("audio_driver_handle, audio_ap_mask", release)
        audio_start = kernel.split("if (audio_device_available)", 1)[1]
        audio_start = audio_start.split(
            "Publish every supervised service", 1)[0]
        self.assertIn(".heartbeat_timeout_ms = 5000U", audio_start)
        self.assertIn(".recovery_timeout_ms = 1000U", audio_start)
        self.assertIn(".restart_budget = 3U", audio_start)
        self.assertIn("post_ready_cpu_affinity_mask", supervisor)
        output = supervisor.split(
            "bool supervisor_device_driver_output_allowed(", 1)[1]
        output = output.split(
            "bool supervisor_device_driver_command_allowed(", 1)[0]
        self.assertIn('strcmp(runtime->name, "hda-ring3") == 0', output)
        self.assertIn("REIST_AUDIO HDA_AP_EXEC cpu=%u epoch=%u", output)

    def test_hda_ap_restart_fault_is_compile_time_bounded(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        supervisor = self.read("kernel/init/supervisor.c")
        runner = self.read("scripts/run_qemu_pci_audio.py")
        self.assertIn("HDA_SMP_LIFECYCLE_FAULT_INJECTION ?= 0", makefile)
        self.assertIn("REIST_HDA_SMP_LIFECYCLE_FAULT_INJECTION", supervisor)
        self.assertIn("HdaSmpLifecycleFaultInjection", windows)
        self.assertIn("HDA_TIMEOUT_ARMED", supervisor)
        self.assertIn("expect-hda-smp-restart", runner)
        self.assertIn("REIST_AUDIO DRIVER_RESTARTED", runner)
        profile = self.read("kernel/init/audio_device_profile.c")
        self.assertIn("device_domain_reset_policy_t reset_policy", profile)
        self.assertIn(".offset = HDA_GCTL", profile)
        self.assertIn(".max_polls = 100U", profile)
        self.assertIn("device_domain_install_reset_policy", profile)

    def test_rotated_audio_services_are_post_ready_ap_affined(self):
        supervisor = self.read("kernel/init/supervisor.c")
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("post_ready_cpu_affinity_mask", supervisor)
        self.assertIn("supervisor_set_audio_service_current_affinity", kernel)
        self.assertIn("REIST_AUDIO SERVICE_AP_EXEC cpu=%u epoch=%u", supervisor)
        handoff = supervisor.split("static bool audio_service_return_to_bsp", 1)[1]
        handoff = handoff.split("static bool audio_service_fence_apply_internal", 1)[0]
        self.assertIn("TASK_CPU_MASK_BSP", handoff)
        self.assertIn("scheduler_sleep_ms(1U)", handoff)
        rotate = supervisor.split(
            "static bool audio_service_rotate_session(supervisor_handle_t handle) {",
            1)[1]
        rotate = rotate.split("bool supervisor_start_audio_service", 1)[0]
        self.assertLess(rotate.index("audio_service_return_to_bsp(&control)"),
                        rotate.index("scheduler_preempt_disable()"))

    def test_audio_service_ap_fault_is_compile_time_bounded(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        supervisor = self.read("kernel/init/supervisor.c")
        runner = self.read("scripts/run_qemu_pci_audio.py")
        self.assertIn("AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION ?= 0", makefile)
        self.assertIn("AudioServiceSmpLifecycleFaultInjection", windows)
        self.assertIn("REIST_AUDIO_SERVICE_SMP_LIFECYCLE_FAULT_INJECTION", supervisor)
        self.assertIn("SERVICE_TIMEOUT_ARMED", supervisor)
        self.assertIn("expect-audio-service-smp-restart", runner)

    def test_tools_sdk_and_virtual_hda_are_packaged(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        sdk = self.read("scripts/build_user_sdk.py")
        runner = self.read("scripts/run_qemu_pci_audio.py")
        audiotest = self.read("userspace/programs/audiotest.c")
        wavplay = self.read("userspace/programs/wavplay.c")
        soundplayer = self.read("userspace/gui/apps/sound_player/main.c")
        wave = self.read("userspace/audio/lib/audio_wave.c")
        vmware = self.read("scripts/create_native_boot_image.py")
        for path in ("sbin/audioinfo.prg", "usr/bin/audiotest.prg",
                     "usr/bin/wavplay.prg",
                     "libexec/reist/hda.prg", "libexec/reist/audio.prg"):
            self.assertIn(path, makefile)
            self.assertIn(f"'{path}'", windows)
        self.assertIn("usr/share/sounds/440hz.wav", makefile)
        self.assertIn("usr/share/sounds/440hz.wav", windows)
        self.assertIn("libreistaudio.a", sdk)
        self.assertIn("reist-audio.pc", sdk)
        self.assertIn("intel-hda,msi=off,debug=1", runner)
        self.assertIn("hda-output,audiodev=reistaudio,debug=1", runner)
        self.assertIn("AUDIO_TEST_CYCLES = 5", runner)
        self.assertIn("TEST_TONE_FRAMES 2400U", audiotest)
        self.assertIn("exactly 22 periods", audiotest)
        self.assertIn("WAVPLAY_DEFAULT_PATH \"/usr/share/sounds/440hz.wav\"",
                      wavplay)
        self.assertIn("REIST_AUDIO_WAVE_CHUNK_LIMIT 16U",
                      self.read("userspace/audio/include/reist/audio_wave.h"))
        self.assertIn("WAVPLAY_PREVIEW_FRAMES 2400U", wavplay)
        self.assertNotIn("malloc", wavplay)
        self.assertIn("REIST_AUDIO_WAVE_CHUNK_LIMIT", wave)
        self.assertIn("reist_audio_wave_load_preview", wavplay)
        self.assertIn("reist_audio_wave_load_preview", soundplayer)
        self.assertIn("reist_gui_control_dispatch", soundplayer)
        self.assertIn("PLAYER_MOUSE_BATCH_LIMIT 32U", soundplayer)
        stop = soundplayer[soundplayer.index("static int stop_audio"):
                           soundplayer.index("static void shutdown_audio")]
        self.assertIn("reist_audio_stop", stop)
        self.assertIn("reist_audio_close", stop)
        self.assertNotIn("reist_audio_shutdown", stop)
        shutdown = soundplayer[soundplayer.index("static void shutdown_audio"):
                               soundplayer.index("static int begin_audio")]
        self.assertIn("reist_audio_shutdown", shutdown)
        self.assertIn("if (!state->audio_initialized)", soundplayer)
        self.assertIn("static void pump_audio", soundplayer)
        self.assertIn("remaining < REIST_AUDIO_MESSAGE_FRAMES", soundplayer)
        self.assertIn("pump_audio(&state)", soundplayer)
        self.assertIn("else if (!state.uploading)", soundplayer)
        self.assertNotIn("malloc", soundplayer)
        self.assertIn('sound.virtualDev = "hdaudio"', vmware)
        self.assertIn('sound.pciSlotNumber = "34"', vmware)
        self.assertIn('usb.generic.allowHID = "FALSE"', vmware)
        runtime = self.read("scripts/test-reist-runtime.ps1")
        self.assertIn("function Invoke-VmwareAudioService", runtime)
        self.assertIn("$attempt -le 2", runtime)
        self.assertIn("$startFailures", runtime)
        self.assertIn("[System.Diagnostics.ProcessStartInfo]::new()", runtime)
        self.assertIn("WaitForExit(15000)", runtime)
        self.assertIn("VMWARE AUDIO PASS", runtime)
        audio_mode = runtime[runtime.index("'pci-audio' {"):]
        self.assertLess(audio_mode.index("Invoke-VmwareAudioService"),
                        audio_mode.index("& $Python $PciAudioRunner"))

    def test_architecture_and_work_package_document_support_boundary(self):
        architecture = self.read("docs/architecture/AUDIO_SUBSYSTEM.md")
        package = self.read("docs/development/PCI_AUDIO_WORK_PACKAGE.md")
        for term in ("Ring 3", "S16_LE", "48 kHz", "Generation",
                     "Bus-Mastering", "libreistaudio"):
            self.assertIn(term, architecture)
        self.assertIn("QEMU", package)
        self.assertIn("VMware", package)


if __name__ == "__main__":
    unittest.main()
