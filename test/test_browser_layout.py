"""R3.28: actual pinned parser -> computed values -> geometry, O0 and O2."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_css_engine import build_css_host, ROOT
from measure_cpp_baseline import suppress_windows_test_dialogs


class BrowserLayoutTests(unittest.TestCase):
    def test_values_and_layout(self):
        suppress_windows_test_dialogs()
        for optimization in ("-O0", "-O2"):
            with self.subTest(optimization=optimization), tempfile.TemporaryDirectory(prefix="reist-layout-") as temp:
                executable = build_css_host(self, Path(temp), optimization, layout=True)
                for mode in ("variables", "cascade", "cycles", "tokens", "inheritance", "malformed", "limits", "expansion", "overflow", "boxes",
                             "flex", "flex-wrap", "flex-column", "flex-align", "flex-reverse", "flex-shrink", "grid", "grid-wide", "grid-narrow", "grid-minimum", "nested",
                             "decoration", "shadow", "fixture", "fixture-narrow", "fonts", "font-fallback", "font-scene",
                             "high-resolution-0", "high-resolution-1", "high-resolution-2", "high-resolution-3"):
                    with self.subTest(mode=mode):
                        result = subprocess.run([str(executable), "layout-"+mode], cwd=ROOT,
                                                capture_output=True, text=True, timeout=15)
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        self.assertIn("BROWSER_LAYOUT_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
