"""Reference VM RAM defaults; explicit low-memory profiles remain supported."""

import ast
import importlib
import inspect
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import run_qemu_smoke as smoke


class VmMemoryDefaultsTests(unittest.TestCase):
    def assert_ram(self, command, expected="1024M"):
        self.assertEqual(command.count("-m"), 1)
        self.assertEqual(command[command.index("-m") + 1], expected)

    def test_common_command_and_run_default_to_one_gib(self):
        self.assert_ram(smoke.qemu_command(Path("qemu"), Path("system.img")))
        self.assertEqual(inspect.signature(smoke.run).parameters["memory"].default,
                         "1024M")

    def test_explicit_low_memory_profiles_remain_unchanged(self):
        for size in ("32M", "64M", "128M", "256M", "512M", "1024M"):
            with self.subTest(size=size):
                self.assert_ram(smoke.qemu_command(
                    Path("qemu"), Path("system.img"), memory=size), size)

    def test_standalone_reference_commands_use_one_gib(self):
        qemu, disk, auxiliary = Path("qemu"), Path("system.img"), Path("aux.img")
        cases = (
            ("run_qemu_admin_maintenance", (qemu, disk, auxiliary, 1234)),
            ("run_qemu_fdd_hotplug", (qemu, disk, auxiliary, 1234)),
            ("run_qemu_sata_hotplug", (qemu, disk, 1234)),
            ("run_qemu_network_shell", (qemu, disk, Path("serial.log"))),
            ("run_qemu_ps2_smoke", (qemu, disk, Path("serial.log"))),
        )
        for module, arguments in cases:
            with self.subTest(module=module):
                self.assert_ram(importlib.import_module(module).qemu_command(*arguments))

    def test_cli_and_runtime_desktop_do_not_override_the_default(self):
        for name in ("run_qemu_smoke.py", "run_qemu_desktop_smoke.py"):
            tree = ast.parse((ROOT / "scripts" / name).read_text(encoding="utf-8"))
            defaults = [ast.literal_eval(kw.value)
                        for node in ast.walk(tree) if isinstance(node, ast.Call)
                        and isinstance(node.func, ast.Attribute)
                        and node.func.attr == "add_argument"
                        and node.args and isinstance(node.args[0], ast.Constant)
                        and node.args[0].value == "--memory"
                        for kw in node.keywords if kw.arg == "default"]
            with self.subTest(module=name):
                self.assertEqual(defaults, ["1024M"])
        tree = ast.parse((ROOT / "scripts/run_qemu_runtime_desktop.py").read_text(
            encoding="utf-8"))
        calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
                 and isinstance(node.func, ast.Name) and node.func.id == "qemu_command"]
        self.assertEqual(len(calls), 1)
        memory = [ast.literal_eval(kw.value) for kw in calls[0].keywords
                  if kw.arg == "memory"]
        self.assertEqual(memory, ["1024M"])

    def test_windows_and_makefile_run_defaults_match(self):
        windows = (ROOT / "scripts/run-windows.ps1").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("'-m', '1024M'", windows)
        self.assertIn("QEMU_COMMON := -m 1024M ", makefile)
        self.assertIn("@$(QEMU) -m 1024M -boot a", makefile)
        self.assertNotIn("-m 512M", makefile)


if __name__ == "__main__":
    unittest.main()
