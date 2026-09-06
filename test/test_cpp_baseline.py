"""Regression checks for the explicitly lexical migration inventory."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from measure_cpp_baseline import measure_text, artifact_info, suppress_windows_test_dialogs


class BaselineTests(unittest.TestCase):
    def test_windows_child_inherits_noninteractive_error_mode_without_a_crash(self):
        import os
        import subprocess
        suppress_windows_test_dialogs()
        if os.name == "nt":
            command = "import ctypes; k=ctypes.WinDLL('kernel32'); print(k.GetErrorMode() & 0x8003)"
            mode = subprocess.check_output([sys.executable, "-c", command],
                creationflags=subprocess.CREATE_NO_WINDOW, text=True, timeout=5)
            self.assertEqual(int(mode), 0x8003)

    def test_comments_and_literals_do_not_invent_cleanup(self):
        text = '/* if (x) free(x); */\nconst char *s="goto fail; close(x)";\n' \
               '// return -1;\nif (bad) { free(p); return -2; }\n'
        m = measure_text(text)
        self.assertEqual(m["physical_lines"], 4)
        self.assertEqual(m["code_lines"], 2)
        self.assertEqual(m["if_tokens"], 1)
        self.assertEqual(m["goto_tokens"], 0)
        self.assertEqual(m["negative_return_tokens"], 1)
        self.assertEqual(m["cleanup_mentions"], {"free": 1})

    def test_lifecycle_and_state_counts_are_explicit_proxies(self):
        text = 'void widget_init(state_t *s);\nvoid widget_destroy(state_t *s);\n' \
               'typedef struct hidden hidden_t;\ntypedef struct state { int x; } state_t;\n'
        m = measure_text(text)
        self.assertEqual(m["init_mentions"], {"widget_init": 1})
        self.assertEqual(m["cleanup_mentions"], {"widget_destroy": 1})
        self.assertEqual(m["opaque_typedefs"], 1)
        self.assertEqual(m["struct_typedefs"], 2)
        self.assertEqual(m["state_pointer_mentions"], 2)

    def test_crlf_and_empty_input(self):
        self.assertEqual(measure_text("int x;\r\n"), measure_text("int x;\n"))
        self.assertEqual(measure_text("")["physical_lines"], 0)

    def test_artifact_size_is_not_memory_size(self):
        import struct
        data = struct.pack("<4s6I", b"MYPR", 0xDEADBEEF, 28, 4096, 0x40000000, 29, 0) + b"x"
        result = artifact_info("test.PRG", data)
        self.assertEqual(result["file_bytes"], 29)
        self.assertEqual(result["loader_payload_bytes"], 4096)
        self.assertEqual(len(result["sha256"]), 64)
        with self.assertRaises(ValueError):
            artifact_info("bad.PRG", b"bad")


if __name__ == "__main__":
    unittest.main()
