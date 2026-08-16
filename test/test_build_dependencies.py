"""Regression tests for complete incremental kernel header dependencies."""

from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_dependency_validator",
    ROOT / "scripts/validate_build_dependencies.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATOR)


class BuildDependencyTests(unittest.TestCase):
    def test_windows_build_reuses_matching_configuration(self) -> None:
        script = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8")
        self.assertIn("[switch]$Clean", script)
        self.assertIn(".windows-build-config.json", script)
        self.assertIn("$requiresClean = $Clean -or", script)
        self.assertIn("--incremental", script)
        self.assertNotIn("& $Make 'clean' \"SHELL=$(To-MakePath $MsysShell)\"\n    if", script)

    def test_userspace_incremental_dependencies_include_sdk_and_linker(self) -> None:
        builder = (ROOT / "scripts/build_user_program.py").read_text(
            encoding="utf-8")
        self.assertIn("dependencies = [*all_sources, linker_script]", builder)
        self.assertIn('glob("*.h")', builder)
        self.assertIn("dependency.stat().st_mtime_ns <=", builder)

    def test_make_emits_explicit_dependency_files_for_every_c_rule(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("DEPFLAGS = -MMD -MP -MF $(@:.o=.d) -MT $@", makefile)
        self.assertIn("DEPS := $(C_OBJ:.o=.d)", makefile)
        self.assertIn("-include $(DEPS)", makefile)
        self.assertIn("kernel: check-kernel-dependencies", makefile)
        self.assertIn("$(OUTPUT_DIR)/kernel.bin: $(ALL_OBJ) $(KERNEL_LDSCRIPT)",
                      makefile)
        self.assertGreaterEqual(
            makefile.count("@$(CC) $(CFLAGS) $(DEPFLAGS) $< -o $@"), 20)
        self.assertNotIn("@$(CC) $(CFLAGS) $< -o $@", makefile)

    def test_valid_dependency_file_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "build/module.d"
            path.parent.mkdir()
            path.write_text(
                "build/module.o: kernel/module.c include/module.h\n",
                encoding="utf-8")
            self.assertEqual([], VALIDATOR.validate([Path("build/module.d")], root))

    def test_missing_source_wrong_target_and_escape_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "module.d"
            path.write_text("wrong.o: include/module.h\n", encoding="utf-8")
            errors = VALIDATOR.validate(
                [Path("module.d"), Path("missing.d"), Path("../escape.d")], root)
        self.assertTrue(any("target mismatch" in error for error in errors))
        self.assertTrue(any("source missing" in error for error in errors))
        self.assertTrue(any("missing dependency" in error for error in errors))
        self.assertTrue(any("escapes repository" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
