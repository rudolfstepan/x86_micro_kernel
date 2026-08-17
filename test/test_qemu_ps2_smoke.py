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
        self.assertEqual(
            MODULE.LOCK_KEY_COMMANDS,
            ("sendkey num_lock\n", "sendkey num_lock\n"),
        )
        self.assertEqual(
            MODULE.monitor_text_commands("cd"),
            ["sendkey c\n", "sendkey d\n"],
        )
        self.assertEqual(
            MODULE.monitor_key_commands("devctl list")[-5:],
            ["sendkey l\n", "sendkey i\n", "sendkey s\n", "sendkey t\n",
             "sendkey ret\n"],
        )

    def test_history_uses_physical_cursor_keys_and_draft_marker(self):
        source = (ROOT / "scripts/run_qemu_ps2_smoke.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"sendkey up\\n"', source)
        self.assertIn('"sendkey down\\n"', source)
        self.assertIn("cursor-up did not recall", source)
        self.assertIn("cursor-down did not restore", source)
        self.assertEqual(MODULE.SHELL_DRAFT_MARKER, "Usage: cd <directory>")
        self.assertIn("lowercase devctl did not receive admin authority", source)
        self.assertIn("devctl did not reject protected root", source)
        self.assertEqual(MODULE.ADMIN_LIST_MARKER, "ADMIN RESOURCE ")
        self.assertEqual(MODULE.ROOT_PROTECTED_MARKER, "ADMIN ROOT_PROTECTED")

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
            "LOCK_KEY_COMMANDS",
        ):
            self.assertIn(marker, source)


if __name__ == "__main__":
    unittest.main()
