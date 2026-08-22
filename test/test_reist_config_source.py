import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReistConfigSourceTests(unittest.TestCase):
    def test_bounded_parser_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-config-") as temp:
            executable = Path(temp) / "config-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Iuserspace/config/include",
                 "test/test_reist_config_host.c",
                 "userspace/config/lib/config.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_service_uses_atomic_same_directory_publication(self):
        source = (ROOT / "userspace/services/config/config_service.c").read_text(
            encoding="utf-8")
        self.assertIn("x86os_create(temp_path)", source)
        self.assertIn("x86os_fsync(descriptor)", source)
        self.assertIn("x86os_rename(temp_path, target->path)", source)
        self.assertIn("x86os_unlink(temp_path)", source)
        self.assertIn("CONFIG_WRITE_CAPACITY", source)
        self.assertNotRegex(source, r"\b(malloc|calloc|realloc|free)\s*\(")

    def test_service_restricts_paths_schemas_keys_and_values(self):
        source = (ROOT / "userspace/services/config/config_service.c").read_text(
            encoding="utf-8")
        for path in ("/etc/reist/system.conf", "/etc/reist/input.conf",
                     "/etc/reist/desktop.conf"):
            self.assertIn(path, source)
        for schema in ("reist.system/1", "reist.input/1", "reist.desktop/1"):
            self.assertIn(schema, source)
        self.assertIn("validate_setting", source)
        self.assertIn("reist_config_set", source)
        self.assertNotIn("filetypes.conf", source)


if __name__ == "__main__":
    unittest.main()
