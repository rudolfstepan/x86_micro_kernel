import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run_qemu_ps2_smoke.py"
SPEC = importlib.util.spec_from_file_location("run_qemu_ps2_smoke", RUNNER)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class QemuPs2SmokeContractTests(unittest.TestCase):
    def test_command_is_injected_as_sequential_ps2_keys(self):
        self.assertEqual(
            MODULE.monitor_key_commands("help"),
            ["sendkey h\n", "sendkey e\n", "sendkey l\n",
             "sendkey p\n", "sendkey ret\n"],
        )

    def test_qemu_uses_monitor_for_keys_and_file_for_serial(self):
        command = MODULE.qemu_command(
            Path("qemu"), Path("disk.img"), Path("serial.log"))
        joined = " ".join(map(str, command))
        self.assertIn("-monitor stdio", joined)
        self.assertIn("-serial file:serial.log", joined)
        self.assertIn("-nic none", joined)
        self.assertIn("snapshot=on", joined)

    def test_success_requires_init_prompt_and_shell_response(self):
        source = RUNNER.read_text(encoding="utf-8")
        for marker in (
            "KEYBOARD_READY_MARKER", "SHELL_PROMPT", "SHELL_HELP_MARKER",
            "userspace shell did not receive PS/2 help command",
        ):
            self.assertIn(marker, source)


if __name__ == "__main__":
    unittest.main()
