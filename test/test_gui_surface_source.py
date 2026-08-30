#!/usr/bin/env python3
import shutil
import subprocess
import tempfile
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
        self.assertIn("REIST_GUI_SURFACE_MAX_PAINT_COMMANDS 192U", header)
        self.assertIn(
            "REIST_GUI_SURFACE_MAX_OVERLAY_PAINT_COMMANDS 96U", header)
        self.assertIn(
            "REIST_GUI_SURFACE_MAX_DYNAMIC_PAINT_COMMANDS 192U", header)
        self.assertIn(
            "REIST_GUI_SURFACE_MAX_HOVER_PAINT_COMMANDS 16U", header)
        self.assertIn("REIST_GUI_SURFACE_PAINT_BEGIN", header)
        self.assertIn("REIST_GUI_SURFACE_PAINT_COMMIT", header)
        self.assertIn("REIST_GUI_SURFACE_PAINT_OVERLAY_BEGIN", header)
        self.assertIn("REIST_GUI_SURFACE_PAINT_OVERLAY_COMMIT", header)
        self.assertIn("sizeof(reist_gui_surface_message_t) <= 128U", header)
        self.assertIn("global window coordinates never cross the ABI", header)

    def test_client_wrapper_has_no_display_access(self):
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        desktop = (ROOT / "userspace/gui/compositor/desktop_surface.h").read_text()
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
        self.assertIn("reist_gui_surface_client_paint_fill", client)
        self.assertIn("reist_gui_surface_client_paint_text", client)
        self.assertIn("reist_gui_surface_client_paint_begin_layer", client)
        self.assertIn("reist_gui_surface_client_paint_commit_layer", client)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC", client)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_HOVER", client)
        self.assertIn("committed_dynamic_paint", desktop)
        self.assertIn("committed_hover_paint", desktop)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY", client)
        self.assertIn("reist_gui_surface_client_accept_configure", client)

    def test_configure_ack_waits_for_compositor_confirmation(self):
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        start = client.index("int reist_gui_surface_client_ack_configure(")
        end = client.index("int reist_gui_surface_client_buffer_create(", start)
        ack = client[start:end]
        self.assertIn("receive_wire_message(client, &response, 500U)", ack)
        self.assertIn("defer_message(client, &response)", ack)
        self.assertIn("REIST_GUI_SURFACE_ACK_CONFIGURE", ack)
        self.assertIn("response.serial == serial", ack)
        self.assertLess(
            ack.index("receive_wire_message(client, &response, 500U)"),
            ack.index("client->acknowledged_serial = serial"),
        )

    def test_dialog_surface_is_parented_and_generation_scoped(self):
        protocol = (ROOT / "userspace/gui/include/reist/gui/surface.h").read_text()
        client = (ROOT / "userspace/gui/lib/surface_client.c").read_text()
        manager = (ROOT / "userspace/gui/compositor/desktop_surface.c").read_text()
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text()
        self.assertIn("REIST_GUI_SURFACE_ROLE_DIALOG", protocol)
        self.assertIn("parent_surface", protocol)
        self.assertIn("reist_gui_surface_client_init_shared", client)
        self.assertIn("reist_gui_surface_client_create_dialog", client)
        self.assertIn("deferred_response_type", client)
        self.assertIn("asynchronous_paint_error", client)
        self.assertIn("desktop_surface_create_dialog", manager)
        self.assertIn("candidate->parent.generation == parent.generation", manager)
        self.assertIn("surface->role == REIST_GUI_SURFACE_ROLE_DIALOG", desktop)

    def test_surface_manager_host_behavior(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        for source in ("test/test_desktop_surface_host.c",
                       "test/test_desktop_surface_dispatch.c"):
            with tempfile.TemporaryDirectory(prefix="reist-surface-") as temp:
                executable = Path(temp) / "surface-test.exe"
                subprocess.run([
                    compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I.", "-Iuserspace/gui/include", source,
                    "userspace/gui/compositor/desktop_surface.c",
                    "-o", str(executable)], cwd=ROOT, check=True,
                    capture_output=True, text=True)
                subprocess.run([str(executable)], cwd=ROOT, check=True,
                               capture_output=True, text=True, timeout=5)

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
