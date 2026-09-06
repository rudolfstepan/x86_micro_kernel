"""Real form model/entry-list behavior; no replacement model in the test."""
import unittest
from test_gui_browser_source import run_host


class BrowserFormsTests(unittest.TestCase):
    def test_model_values_and_submission(self):
        run_host(["test/test_browser_forms_host.c",
                  "userspace/gui/apps/browser/browser_forms.c",
                  "userspace/gui/lib/html_document.c"])


if __name__ == "__main__":
    unittest.main()
