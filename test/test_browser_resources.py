import unittest
from test_gui_browser_source import run_host


class BrowserResourcesTests(unittest.TestCase):
    def test_real_bundle_authority_quotas_and_generation(self):
        run_host(["test/test_browser_resources_host.c", "userspace/gui/apps/browser/browser_resources.c",
                  "userspace/gui/lib/html_document.c", "userspace/programs/curl_http.c"], flags=["-I."])


if __name__ == "__main__":
    unittest.main()
