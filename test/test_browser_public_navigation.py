import unittest
from test_gui_browser_source import run_host


class PublicNavigationTests(unittest.TestCase):
    def test_resource_wire_compatibility_and_authority(self):
        run_host(["test/test_browser_resources_host.c", "userspace/gui/apps/browser/browser_resources.cpp",
                  "userspace/gui/lib/html_document.c", "userspace/programs/curl_http.c"], flags=["-I."])

    def test_real_response_and_private_worker_admission(self):
        run_host(["test/test_browser_public_host.c",
                  "userspace/gui/apps/browser/browser_response.cpp",
                  "userspace/gui/apps/browser/browser_scene.c",
                  "userspace/gui/apps/browser/browser_forms.c",
                  "userspace/gui/apps/browser/html_protocol.c",
                  "userspace/programs/curl_http.c", "userspace/gui/lib/html_document.c",
                  "userspace/gui/lib/font.c"], flags=["-I."])


if __name__ == "__main__":
    unittest.main()
