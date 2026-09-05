import unittest
import importlib.util
from pathlib import Path
from unittest.mock import patch
from test_gui_browser_source import run_host


class BrowserNavigationTests(unittest.TestCase):
    def test_guest_peer_requires_http11_and_decoded_marker(self):
        path = Path(__file__).resolve().parents[1] / "scripts/run_qemu_smoke.py"
        spec = importlib.util.spec_from_file_location("browser_curl_peer", path)
        peer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(peer)
        request = b"GET /data.txt HTTP/1.1\r\nHost: 10.0.2.101\r\nAccept-Encoding: identity\r\n\r\n"
        segments = [(1234, 80, 100, 0, 2, b""), (1234, 80, 101, 12001, 24, request),
                    (1234, 80, 101 + len(request), 12001, 17, b"")]
        with patch.object(peer, "receive_tcp_segment", side_effect=segments), \
             patch.object(peer, "inject_ethernet_frame", return_value=True), \
             patch.object(peer, "tcp_peer_frame", return_value=b"frame") as frames:
            self.assertIsNone(peer.serve_curl_test_client(None, 1000))
        self.assertEqual(frames.call_count, 3)  # SYN/ACK, response, FIN/ACK remain mandatory.
        response = frames.call_args_list[1].args[-1]
        head, body = response.split(b"\r\n\r\n", 1)
        self.assertIn(peer.CURL_TEST_HEADER_MARKER.encode(), head)
        self.assertNotIn(peer.CURL_TEST_REPLY_MARKER.encode(), body)
        self.assertIn(b"Transfer-Encoding: chunked", head)
        decoded = b""
        while True:
            size, body = body.split(b"\r\n", 1)
            count = int(size.split(b";")[0], 16)
            if not count:
                self.assertEqual(body, b"\r\n")
                break
            decoded += body[:count]
            self.assertEqual(body[count:count+2], b"\r\n")
            body = body[count+2:]
        self.assertEqual(decoded, peer.CURL_TEST_REPLY_MARKER.encode() + b"\n")

    def test_curl_url_and_header_parser(self):
        run_host(["test/test_curl_http_host.c", "userspace/programs/curl_http.c"], flags=["-I."])

    def test_response_metadata_redirects_and_representation(self):
        run_host(["test/test_browser_response_host.c",
                  "userspace/gui/apps/browser/browser_response.c",
                  "userspace/programs/curl_http.c",
                  "userspace/gui/lib/html_document.c"], flags=["-I."])

    def test_real_curl_stream_framing(self):
        run_host(["test/test_curl_stream_host.c", "userspace/programs/curl_http.c"],
                 flags=["-I.", "-Iuserspace/sdk/include", "-Iuserspace/tls/include",
                        "-Wno-unused-function"])


if __name__ == "__main__":
    unittest.main()
