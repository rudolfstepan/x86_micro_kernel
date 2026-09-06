#!/usr/bin/env python3
import shutil
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class GuiSurfaceSourceTests(unittest.TestCase):
    def test_real_compositor_routes_wheel_after_pending_motion(self):
        from test_gui_browser_source import run_host
        desktop = (ROOT / "userspace/gui/compositor/desktop.c").read_text()
        start = desktop.index("            if (mouse.wheel != 0")
        block = desktop[start:desktop.index("            previous_buttons = mouse.buttons;", start)]
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "reist/gui/surface.h"
#define DESKTOP_WM_CAPTURE_NONE 0
#define DESKTOP_WM_CAPTURE_CLIENT 1
#define DESKTOP_DRAG_PHASE_IDLE 0
#define DESKTOP_WM_CAPACITY 2
#define DESKTOP_SURFACE_ECAPACITY -75
#define DESKTOP_EXPLORER_OK 0
typedef reist_gui_rect_t desktop_rect_t;
typedef struct { int scroll_enabled; reist_gui_surface_owner_t owner; } desktop_surface_slot_t;
typedef struct { int unused; } desktop_explorer_result_t;
static desktop_surface_slot_t slot={.scroll_enabled=1};
static unsigned motions, client_motions, wheels, explorer_wheels, fenced;
static int wheel_window, wheel_x, wheel_y, wheel_delta, overflow;
static int desktop_ui_owns_pointer(int *ui) { return *ui; }
unsigned dispatch_pointer_motion(void *m,void *e,void *u,void *d,void *dirty,
    int32_t *x,int32_t *y,int32_t dx,int32_t dy,void *t,void *drag,void *resize,void *cache) {
    (void)m;(void)e;(void)u;(void)d;(void)dirty;(void)t;(void)drag;(void)resize;(void)cache;
    ++motions; *x+=dx; *y+=dy; return 0;
}
static int desktop_wm_window_at(void *m,int x,int y) { (void)m;(void)y; return x<100 ? 0 : 1; }
static desktop_surface_slot_t *surface_for_window(void *s,unsigned w) { (void)s;(void)w; return &slot; }
static unsigned enqueue_surface_pointer(void *m,void *s,int w,unsigned type,
    int x,int y,int dx,int dy,unsigned pressed,unsigned capture,int *status) {
    (void)m;(void)s;
    if (type==REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        assert(x==110 && y==60 && dx==30 && dy==-20 && !pressed);
        assert(!status && !wheels); ++client_motions; return 1;
    }
    assert(type==REIST_GUI_SURFACE_INPUT_POINTER_SCROLL && client_motions==1);
    assert(!dx && !pressed && !capture);
    ++wheels; wheel_window=w; wheel_x=x; wheel_y=y; wheel_delta=dy;
    *status=overflow ? DESKTOP_SURFACE_ECAPACITY : 0; return !overflow;
}
static void desktop_surface_revoke_owner(void *s,reist_gui_surface_owner_t o) { (void)s;(void)o; ++fenced; }
static void desktop_dirty_full(void *d) { (void)d; }
static void x86os_puts(const char *t) { (void)t; }
static desktop_rect_t desktop_explorer_content_rect(void *m,void *e,unsigned w) {
    (void)m;(void)e;(void)w; return (desktop_rect_t){0,0,200,200};
}
static int point_in_rect(desktop_rect_t r,int x,int y) { return x>=r.x && y>=r.y && x<200 && y<200; }
static void desktop_explorer_result_initialize(desktop_explorer_result_t *r) { (void)r; }
static int desktop_explorer_wheel(void *e,unsigned w,desktop_rect_t c,int d,desktop_explorer_result_t *r) {
    (void)e;(void)w;(void)c;(void)d;(void)r; ++explorer_wheels; return 0;
}
static void collect_explorer_pointer_result(void *d,void *m,void *dirty,desktop_explorer_result_t *r,unsigned b,void *a) {
    (void)d;(void)m;(void)dirty;(void)r;(void)b;(void)a;
}
static void run(int wheel,int menu,int capture,int drag_phase,int enabled,int fail) {
    struct { int wheel; } mouse={wheel};
    struct { int capture_kind, capture_window; } manager={capture,0};
    struct { int phase; } desktop_drag={drag_phase};
    int ui=menu,explorer=0,display=0,dirty=0,move_cache=0,surfaces=0,activation=0;
    uint32_t actions=0,action_target=0,drag_render=0,resize_render=0,surface_input_queued=0;
    int32_t pointer_x=80,pointer_y=80,pending_delta_x=30,pending_delta_y=-20;
    motions=client_motions=wheels=explorer_wheels=fenced=0; slot.scroll_enabled=enabled; overflow=fail;
    @BLOCK@
    (void)actions;(void)action_target;(void)drag_render;(void)resize_render;(void)move_cache;
    if (wheel) {
        assert(pointer_x==110 && pointer_y==60 && !pending_delta_x && !pending_delta_y && motions==1);
        assert(client_motions==1); /* Legacy/disabled-wheel clients keep motion. */
        if (!menu && !capture && !drag_phase && enabled) {
            assert(wheels==1 && wheel_window==1 && wheel_x==110 && wheel_y==60);
            int64_t expected=-(int64_t)wheel*120;
            if(expected>INT32_MAX) expected=INT32_MAX;
            if(expected<INT32_MIN) expected=INT32_MIN;
            assert(wheel_delta==(int32_t)expected && fenced==(unsigned)fail);
            assert(surface_input_queued); /* Earlier motion remains accounted. */
        } else assert(!wheels && !fenced);
        assert(explorer_wheels==(unsigned)(!menu && !capture && !drag_phase));
    } else assert(!motions && !wheels && pointer_x==80 && pending_delta_x==30);
}
int main(void) {
    run(-1,0,0,0,1,0); run(1,0,0,0,1,0);
    run(INT32_MIN,0,0,0,1,0); run(INT32_MAX,0,0,0,1,0);
    run(1,1,0,0,1,0); run(1,0,1,0,1,0); run(1,0,0,1,1,0);
    run(1,0,0,0,0,0); run(1,0,0,0,1,1); run(0,0,0,0,1,0);
    puts("COMPOSITOR_WHEEL_ORDER_HOST_OK"); return 0;
}
'''.replace("@BLOCK@", block)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "compositor-wheel-order-host.c"
            source.write_text(harness, encoding="utf-8")
            run_host([str(source)])

    def test_client_drains_input_backpressure_without_losing_events(self):
        from test_gui_browser_source import run_host
        run_host(["test/test_gui_surface_client_host.c",
                  "userspace/gui/lib/surface_client.c",
                  "userspace/gui/lib/font_catalog.c"],
                 flags=["-Iuserspace/sdk/include"])

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
        self.assertIn("REIST_GUI_SURFACE_PAINT_FONT_TEXT", header)
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
        self.assertIn("reist_gui_surface_client_paint_font_text", client)
        self.assertIn(
            "reist_gui_font_catalog_selection_valid(font_family, pixel_height)",
            client)
        self.assertIn("reist_gui_surface_client_paint_begin_layer", client)
        self.assertIn("reist_gui_surface_client_paint_commit_layer", client)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_DYNAMIC", client)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_HOVER", client)
        self.assertIn("committed_dynamic_paint", desktop)
        self.assertIn("committed_hover_paint", desktop)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_OVERLAY", client)
        self.assertIn("reist_gui_surface_client_accept_configure", client)

    def test_surface_broker_keeps_each_desktop_turn_bounded(self):
        runtime = ROOT / "userspace/gui/compositor/desktop_surface_runtime.h"
        header = runtime.read_text()
        self.assertIn("DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS 16U", header)
        self.assertNotIn("DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS 64U", header)

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
        command = [compiler] if compiler else [r"C:\tools\zig-x86_64-windows-0.16.0\zig.exe", "cc"]
        environment = os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/browser-host/zig-global")
        environment["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/browser-host/zig-local")
        for source in ("test/test_desktop_surface_host.c",
                       "test/test_desktop_surface_dispatch.c"):
            with tempfile.TemporaryDirectory(prefix="reist-surface-") as temp:
                executable = Path(temp) / "surface-test.exe"
                result = subprocess.run([
                    *command, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I.", "-Iuserspace/gui/include", source,
                    "userspace/gui/compositor/desktop_surface.c",
                    "userspace/gui/lib/font_catalog.c",
                    "-o", str(executable)], cwd=ROOT, env=environment,
                    capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stderr)
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
