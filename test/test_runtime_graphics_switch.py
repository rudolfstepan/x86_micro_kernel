"""Source contracts for native runtime graphics activation."""

import sys
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from run_qemu_runtime_desktop import (
    convert_screenshot_if_png,
    desktop_monitor_key_commands,
    parse_render_metrics,
)


class RuntimeGraphicsSwitchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.control = (ROOT / "drivers/video/display_control.c").read_text()
        cls.header = (ROOT / "drivers/video/display_control.h").read_text()
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        cls.sdk = (ROOT / "userspace/sdk/x86os.c").read_text()
        cls.desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text()
        cls.display = (ROOT / "drivers/video/display.c").read_text()
        cls.framebuffer = (ROOT / "drivers/video/framebuffer.c").read_text()
        cls.stdlib = (ROOT / "lib/libc/stdlib.h").read_text()
        cls.kernel = (ROOT / "kernel/init/kernel.c").read_text()
        cls.vbe_runtime = (ROOT / "arch/x86/boot/vbe_runtime.asm").read_text()
        cls.runtime_runner = (
            ROOT / "scripts/run_qemu_runtime_desktop.py"
        ).read_text()
        cls.runtime_script = (
            ROOT / "scripts/test-reist-runtime.ps1"
        ).read_text()
        cls.documentation_capture = (
            ROOT / "scripts/capture-documentation.ps1"
        ).read_text()

    def test_append_only_control_abi(self):
        self.assertIn("SYS_DISPLAY_CONTROL 109", self.stdlib)
        self.assertIn("DISPLAY_CONTROL_ABI_VERSION 1U", self.header)
        self.assertIn("DISPLAY_CONTROL_DEACTIVATE 2U", self.header)
        self.assertIn("case SYS_DISPLAY_CONTROL:", self.syscalls)

    def test_firmware_transition_is_confined_to_fixed_kernel_thunk(self):
        self.assertNotIn("int 0x10", self.control.lower())
        self.assertIn("int 0x10", self.vbe_runtime.lower())
        self.assertIn("vbe_runtime_set_mode", self.control)
        self.assertIn("vbe_runtime_set_text_mode", self.control)
        self.assertIn("0x01CEU", self.control)
        self.assertIn("0x01CFU", self.control)
        self.assertIn("0x1234U", self.control)
        self.assertIn("0x1111U", self.control)
        self.assertIn("framebuffer_init_runtime(&info)", self.control)

    def test_activation_is_validated_before_mode_enable(self):
        self.assertLess(
            self.control.index("bool valid"),
            self.control.index("dispi_write(DISPI_ENABLE, 0U);")
        )
        self.assertIn("dispi_read(DISPI_XRES)", self.control)
        self.assertIn("dispi_read(DISPI_ENABLE)", self.control)
        self.assertIn("dispi_write(DISPI_ENABLE, 0U);", self.control)

    def test_desktop_retries_activation_once(self):
        self.assertIn("x86os_display_activate()", self.desktop)
        self.assertLess(
            self.desktop.index("int display_status = x86os_display_info"),
            self.desktop.index("x86os_display_activate()")
        )

    def test_desktop_restores_vga_after_runtime_activation(self):
        self.assertIn("runtime_activated", self.desktop)
        self.assertIn("x86os_display_deactivate()", self.desktop)
        self.assertIn("display_control_deactivate()", self.syscalls)
        self.assertIn("framebuffer_shutdown()", self.control)
        self.assertIn("SVGA_REG_ENABLE, 0U", self.control)
        self.assertIn("dispi_write(DISPI_ENABLE, 0U)", self.control)

    def test_console_backend_is_runtime_selected(self):
        self.assertIn("framebuffer_available()", self.display)
        self.assertIn("#define USE_FRAMEBUFFER 1", self.display)

    def test_vmware_backend_presents_dirty_rectangles_through_fifo(self):
        self.assertIn("VMWARE_DEVICE_SVGA2 0x0405U", self.control)
        self.assertIn("VMWARE_DEVICE_SVGA 0x0710U", self.control)
        self.assertIn("SVGA_REG_CONFIG_DONE", self.control)
        self.assertIn("SVGA_CMD_UPDATE", self.control)
        self.assertIn("SVGA_FIFO_NEXT_CMD", self.control)
        self.assertIn("display_control_present_rect", self.framebuffer)

    def test_unknown_vmware_display_never_enters_runtime_vbe(self):
        self.assertIn("find_vmware_display", self.control)
        self.assertIn("VBE runtime transition suppressed", self.control)
        guard = self.control.index("candidate = find_vmware_display();")
        vbe = self.control.index(
            "if (result != 0 && vbe_prepared) result = activate_vbe();")
        self.assertLess(guard, vbe)

    def test_runtime_mmio_is_prepared_before_userspace(self):
        prepare = self.kernel.index("display_control_prepare();")
        shell = self.kernel.index('start_userspace_program(multiboot_info, "bin/shell.prg"')
        self.assertLess(prepare, shell)
        self.assertIn("vmware_prepared", self.control)
        self.assertIn("qemu_prepared", self.control)

    def test_vbe_lfb_may_be_inside_a_sized_display_bar(self):
        self.assertIn("PCI_COMMAND_MEMORY", self.control)
        self.assertIn("0xFFFFFFFFU", self.control)
        self.assertIn("bar_size", self.control)
        self.assertIn("range_end <= bar_end", self.control)
        self.assertNotIn("if (base == address) return true;", self.control)

    def test_failed_activation_reports_pci_graphics_identity(self):
        self.assertIn("report_unsupported_graphics", self.control)
        self.assertIn("VGA=%04X:%04X", self.control)
        self.assertIn("device->bar[5]", self.control)

    def test_desktop_metrics_parser_accepts_only_complete_bounded_probe(self):
        line = (
            "DESKTOP_METRICS version=1 full_frames=1 full_total_ms=10 "
            "full_max_ms=10 dirty_frames=16 dirty_total_ms=80 "
            "dirty_max_ms=10 drag_frames=8 drag_total_ms=40 "
            "drag_max_ms=10 resize_frames=8 resize_total_ms=40 "
            "resize_max_ms=10 fallback_frames=0 damage_regions=32 "
            "damage_max=2 clock_errors=0 probe_errors=0"
        )
        metrics, normalized = parse_render_metrics(line.replace(" ", "\n"))
        self.assertEqual(metrics["resize_frames"], 8)
        self.assertEqual(metrics["damage_max"], 2)
        self.assertTrue(normalized.startswith("DESKTOP_METRICS version=1"))

        with self.assertRaises(RuntimeError):
            parse_render_metrics(line.replace("resize_frames=8",
                                              "resize_frames=7"))
        with self.assertRaises(RuntimeError):
            parse_render_metrics(line.replace("full_frames=1",
                                              "full_frames=2"))
        with self.assertRaises(RuntimeError):
            parse_render_metrics(line.replace("probe_errors=0",
                                              "probe_errors=1"))

    def test_runtime_metrics_mode_runs_the_fixed_desktop_probe(self):
        self.assertIn("runtime-desktop-metrics", self.runtime_script)
        self.assertIn("Invoke-RuntimeDesktop $false $true",
                      self.runtime_script)
        self.assertIn('"desktop.prg --render-probe"', self.runtime_runner)
        self.assertIn("--metrics-log", self.runtime_runner)
        keys = desktop_monitor_key_commands("desktop.prg --render-probe")
        self.assertEqual(keys.count("sendkey minus\n"), 3)
        self.assertEqual(keys[-1], "sendkey ret\n")

    def test_runtime_surface_mode_starts_a_real_ring3_window(self):
        self.assertIn("runtime-desktop-surface", self.runtime_script)
        self.assertIn("Invoke-RuntimeDesktop $false $false $true",
                      self.runtime_script)
        self.assertIn('"desktop.prg --surface-probe"', self.runtime_runner)
        self.assertIn("DESKTOP_SURFACE_OK", self.runtime_runner)
        self.assertIn("screenshot_has_menu_text", self.runtime_runner)
        self.assertIn("desktop screenshot contains no menu text",
                      self.runtime_runner)

    def test_documentation_capture_uses_runtime_vga_proofs(self):
        self.assertIn("-Target qemu -Video vga", self.documentation_capture)
        self.assertIn("reist-desktop.png", self.documentation_capture)
        self.assertIn("reist-desktop-apps.png", self.documentation_capture)
        self.assertIn("reist-notepad.png", self.documentation_capture)
        self.assertIn("--surface-probe", self.documentation_capture)
        self.assertIn("--notepad-probe", self.documentation_capture)

    def test_runtime_notepad_mode_starts_a_visible_document_window(self):
        self.assertIn('"desktop.prg --notepad-probe"', self.runtime_runner)
        self.assertIn("NOTEPAD_SURFACE_DOCUMENT_READY", self.runtime_runner)
        self.assertIn("runtime-desktop-notepad", self.runtime_runner)

    def test_png_capture_conversion_preserves_dimensions(self):
        with tempfile.TemporaryDirectory(prefix="reist-doc-shot-") as temp:
            path = Path(temp) / "capture.png"
            path.write_bytes(b"P6\n2 2\n255\n" + bytes(range(12)))
            convert_screenshot_if_png(path)
            data = path.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[16:24]), (2, 2))

    def test_documentation_desktop_images_are_versioned_pngs(self):
        directory = ROOT / "docs/assets/screenshots"
        for name in ("reist-desktop.png", "reist-desktop-apps.png",
                     "reist-notepad.png"):
            data = (directory / name).read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[16:24]), (1024, 768))


if __name__ == "__main__":
    unittest.main()
