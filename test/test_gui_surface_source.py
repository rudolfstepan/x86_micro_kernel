#!/usr/bin/env python3
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class GuiSurfaceSourceTests(unittest.TestCase):
    def test_public_protocol_is_bounded_and_local(self):
        header = (ROOT / "userspace/gui/include/reist/gui/surface.h").read_text()
        self.assertIn("REIST_GUI_SURFACE_MAX_DAMAGE 8U", header)
        self.assertIn("REIST_GUI_SURFACE_ACK_CONFIGURE", header)
        self.assertIn("REIST_GUI_SURFACE_BUFFER_RELEASE", header)
        self.assertIn("REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888", header)
        self.assertIn("REIST_GUI_SURFACE_MAX_BUFFER_BYTES", header)
        self.assertIn("sizeof(reist_gui_surface_message_t) <= 128U", header)
        self.assertIn("global window coordinates never cross the ABI", header)

    def test_client_wrapper_has_no_display_access(self):
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        self.assertIn("x86os_ipc_send_timeout", client)
        self.assertIn("x86os_ipc_receive_timeout", client)
        self.assertIn("ipc.version = X86OS_IPC_MESSAGE_VERSION", client)
        self.assertIn("ipc.struct_size = sizeof(ipc)", client)
        self.assertNotIn("x86os_fill_rect", client)
        self.assertNotIn("x86os_draw_pixels", client)
        self.assertIn("acknowledged_serial", client)
        self.assertIn("--reist-surface=", client)
        self.assertIn("UINT32_MAX", client)
        self.assertIn("reist_gui_surface_buffer_validate", client)

    def test_configure_ack_waits_for_compositor_confirmation(self):
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        start = client.index("int reist_gui_surface_client_ack_configure(")
        end = client.index("int reist_gui_surface_client_buffer_create(", start)
        ack = client[start:end]
        self.assertIn("receive_message(client, &response, 500U)", ack)
        self.assertIn("REIST_GUI_SURFACE_ACK_CONFIGURE", ack)
        self.assertIn("response.serial != serial", ack)
        self.assertLess(
            ack.index("receive_message(client, &response, 500U)"),
            ack.index("client->acknowledged_serial = serial"),
        )

    def test_endpoint_bootstrap_is_fail_closed(self):
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        self.assertIn("if (endpoint == 0 || argc < 1 || argv == 0) return -22;", client)
        self.assertIn("if (digits == 0U || text[index] != '\\0' || value == 0U)", client)
        self.assertIn("return -34;", client)

    def test_demo_bounds_spawn_delegation_race(self):
        demo = (ROOT / "userspace/gui/apps/surface_demo/main.c").read_text()
        self.assertIn("attempt < 250U", demo)
        self.assertIn("create_status != -9 && create_status != -13", demo)
        self.assertIn("x86os_sleep_ms(1U)", demo)


if __name__ == "__main__":
    unittest.main()
