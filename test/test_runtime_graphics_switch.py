"""Source contracts for native runtime graphics activation."""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RuntimeGraphicsSwitchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.control = (ROOT / "drivers/video/display_control.c").read_text()
        cls.header = (ROOT / "drivers/video/display_control.h").read_text()
        cls.syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        cls.sdk = (ROOT / "userspace/sdk/x86os.c").read_text()
        cls.desktop = (ROOT / "userspace/programs/desktop.c").read_text()
        cls.display = (ROOT / "drivers/video/display.c").read_text()
        cls.framebuffer = (ROOT / "drivers/video/framebuffer.c").read_text()
        cls.stdlib = (ROOT / "lib/libc/stdlib.h").read_text()
        cls.kernel = (ROOT / "kernel/init/kernel.c").read_text()
        cls.vbe_runtime = (ROOT / "arch/x86/boot/vbe_runtime.asm").read_text()

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
        self.assertIn("framebuffer_init(&info)", self.control)

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
        self.assertIn("SVGA_REG_CONFIG_DONE", self.control)
        self.assertIn("SVGA_CMD_UPDATE", self.control)
        self.assertIn("SVGA_FIFO_NEXT_CMD", self.control)
        self.assertIn("display_control_present_rect", self.framebuffer)

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


if __name__ == "__main__":
    unittest.main()
