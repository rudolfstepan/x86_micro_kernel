import shutil
import subprocess
import tempfile
import unittest
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
             "test/test_audio_host.c"],
            ["-DREIST_HDA_DRIVER_HELPERS_ONLY"],
        )

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
        self.assertIn("scheduler_preempt_disable();", spawn)
        self.assertIn("scheduler_preempt_enable();", spawn)

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
        self.assertIn("HDA_PARAMETER_OUTPUT_AMP_CAPS", driver)
        self.assertIn("driver->fatal", driver)
        self.assertNotIn("k_malloc", driver)

    def test_tools_sdk_and_virtual_hda_are_packaged(self):
        makefile = self.read("Makefile")
        windows = self.read("scripts/build-windows.ps1")
        sdk = self.read("scripts/build_user_sdk.py")
        runner = self.read("scripts/run_qemu_pci_audio.py")
        vmware = self.read("scripts/create_native_boot_image.py")
        for path in ("sbin/audioinfo.prg", "usr/bin/audiotest.prg",
                     "libexec/reist/hda.prg", "libexec/reist/audio.prg"):
            self.assertIn(path, makefile)
            self.assertIn(f"'{path}'", windows)
        self.assertIn("libreistaudio.a", sdk)
        self.assertIn("reist-audio.pc", sdk)
        self.assertIn("intel-hda,msi=off,debug=1", runner)
        self.assertIn("hda-output,audiodev=reistaudio,debug=1", runner)
        self.assertIn("AUDIO_TEST_CYCLES = 5", runner)
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
