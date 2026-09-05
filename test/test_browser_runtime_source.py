import os
import tempfile
import unittest
from pathlib import Path
from test_gui_browser_source import run_host

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "userspace/gui/apps/browser/main.c"

# Include the real application, not a second model of its transport lifecycle.
# Only OS/IPC boundaries are mocked; parsing, publication and polling are real.
TRANSPORT_HOST = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main static browser_application_main
#include "userspace/gui/apps/browser/main.c"
#undef main

static x86os_process_info_t process;
static unsigned exists, generation, identity_exit, kill_exit, waits, kills, clock_ms;
static const char *file_bytes = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<title>Downloaded</title><p>Working page</p>";
static size_t file_length, file_offset;
static unsigned opens, closes;
static unsigned spawns, unlinks, image_decodes;
static char spawned_url[BROWSER_URL_CAPACITY];
static browser_workspace_t host_workspace;

int x86os_getpid(void) { return 77; }
uint32_t x86os_uptime_ms(void) { return clock_ms; }
int x86os_unlink(const char *path) { assert(path[0] && !exists); ++unlinks; return 0; }
int x86os_spawnv(const char *path, int argc, const char *const *argv) {
    assert(!strcmp(path, "/usr/bin/curl.prg") && argc == 7);
    assert(!strcmp(argv[3], "--max-bytes"));
    assert(!strcmp(argv[4], "65536") || !strcmp(argv[4], "262144"));
    assert(!strcmp(argv[5], "--include") && !strchr(argv[6], '#'));
    assert(!exists); ++spawns; strcpy(spawned_url,argv[6]);
    exists = 1; return process.pid;
}
int x86os_process_info(uint32_t index, x86os_process_info_t *info) {
    if (index || !exists) return 0;
    *info = process; return 1;
}
int x86os_process_identity_of(int pid, x86os_process_identity_t *identity) {
    assert(pid == process.pid);
    if (identity_exit) { process.state = X86OS_PROCESS_ZOMBIE; identity_exit = 0; }
    if (!exists || process.state == X86OS_PROCESS_ZOMBIE) return -3;
    *identity = (x86os_process_identity_t){1, sizeof(*identity), pid, generation};
    return 0;
}
int x86os_wait(int pid, int *status) {
    assert(exists && pid == process.pid && process.parent_pid == x86os_getpid());
    assert(process.state == X86OS_PROCESS_ZOMBIE); /* Never block the UI. */
    ++waits; exists = 0; *status = process.exit_status; return pid;
}
int x86os_kill(int pid) {
    assert(exists && pid == process.pid && process.parent_pid == x86os_getpid());
    ++kills; process.state = X86OS_PROCESS_ZOMBIE;
    return kill_exit ? -13 : 0;
}
void x86os_puts(const char *text) { (void)text; }
void x86os_print_number(int number) { (void)number; }
int reist_gui_surface_client_set_title(reist_gui_surface_client_t *client, const char *title) {
    (void)client;
    assert(strlen(title) < REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY);
    return 0;
}
int reist_vfs_file_open_rights(const char *path, uint32_t timeout, uint32_t rights,
                              reist_vfs_file_handle_t *handle) {
    assert(path[0] && timeout && rights == (REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT));
    file_offset = 0; ++opens; *handle = 1; return 0;
}
int reist_vfs_file_fstat(reist_vfs_file_handle_t handle, x86os_file_info_t *info) {
    assert(handle == 1); memset(info, 0, sizeof(*info));
    info->type = X86OS_FILE; info->size = (uint32_t)file_length; return 0;
}
int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *data, size_t capacity) {
    assert(handle == 1);
    if (capacity > file_length - file_offset) capacity = file_length - file_offset;
    memcpy(data, file_bytes + file_offset, capacity); file_offset += capacity;
    return (int)capacity;
}
int reist_vfs_file_close(reist_vfs_file_handle_t handle) { assert(handle == 1); ++closes; return 0; }
int browser_image_decode(const uint8_t *bytes, size_t length, uint32_t *pixels,
                         size_t capacity, reist_image_info_t *info) {
    ++image_decodes;
    if (length != 3 || memcmp(bytes,"PNG",3)) return -22;
    assert(capacity >= 1); memset(info,0,sizeof(*info));
    info->width=info->height=info->stride_pixels=1; pixels[0]=0x123456; return 0;
}
static browser_state_t fresh(void) {
    browser_state_t state = {0};
    process = (x86os_process_info_t){81, 77, X86OS_PROCESS_RUNNING, 0, "curl"};
    exists = waits = kills = identity_exit = kill_exit = 0; generation = 23;
    spawns = unlinks = image_decodes = 0;
    clock_ms = 100; state.loaded = 1; state.image_next = BROWSER_IMAGE_CACHE_COUNT;
    copy_text(state.active_url, sizeof(state.active_url), "/htdocs/index.html");
    make_temporary_path(&state); return state;
}
static void failed_load(int exited_before_identity, int race, uint32_t kind) {
    browser_state_t state = fresh(); reist_gui_surface_client_t client = {0};
    if (exited_before_identity) process.state = X86OS_PROCESS_ZOMBIE;
    identity_exit = (unsigned)race; process.exit_status = 74; /* DNS/transport failure. */
    assert(start_fetch(&state, "https://intracom.at/#details", kind, 0) == 0);
    process.state = X86OS_PROCESS_ZOMBIE;
    service_loads(&state, &client);
    assert(!state.exit_requested && !state.child_pid && waits == 1 && !kills);
    assert(state.loaded && !state.active && !strcmp(state.active_url, "/htdocs/index.html"));
    assert(state.status_redraw);
    service_loads(&state, &client); assert(waits == 1);
}
static void complete(browser_state_t *state, reist_gui_surface_client_t *client, const char *response) {
    assert(exists); file_bytes=response; file_length=strlen(response);
    clock_ms+=10; process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=0;
    service_loads(state,client);
    assert(!exists && !state->child_pid && !state->exit_requested && opens==closes);
}
static void follow(browser_state_t *state, reist_gui_surface_client_t *client) {
    assert(state->follow_redirect && !exists);
    unsigned before=unlinks;
    process.state=X86OS_PROCESS_RUNNING; service_loads(state,client);
    assert(exists && state->child_pid && !state->follow_redirect && unlinks==before+2);
}
static void redirect_cases(void) {
    reist_gui_surface_client_t client={0}; client.width=800; client.height=600;
    browser_state_t state=fresh();
    assert(start_fetch(&state,"https://example.test/start#section",1,0)==0);
    uint32_t deadline=state.redirect_deadline;
    clock_ms+=5;
    assert(start_fetch(&state,"https://other.test/",1,0)==-16);
    assert(state.redirect_deadline==deadline && spawns==1);
    state.address_focused=1; strcpy(state.address,"https://typed.test/");
    complete(&state,&client,"HTTP/1.1 301 Moved\r\nLocation: /new/index.html\r\nContent-Length: 0\r\n\r\n");
    assert(!state.active && !strcmp(state.active_url,"/htdocs/index.html"));
    assert(spawns==1 && waits==1 && unlinks==4 && state.redirect_count==1);
    assert(!strcmp(state.job_url,"https://example.test/new/index.html#section"));
    assert(!strcmp(state.address,"https://typed.test/"));
    follow(&state,&client);
    assert(state.redirect_deadline==deadline && !strcmp(spawned_url,"https://example.test/new/index.html"));
    complete(&state,&client,"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<p id='section'><a href='next.html'>Next</a><img src='icon.png'></p>");
    assert(state.active==1 && !strcmp(state.active_url,"https://example.test/new/index.html#section"));
    assert(!strcmp(state.address,"https://typed.test/"));
    char resolved[BROWSER_URL_CAPACITY];
    assert(reist_html_url_resolve(state.active_url,"next.html",resolved,sizeof(resolved))==0);
    assert(!strcmp(resolved,"https://example.test/new/next.html"));
    process.state=X86OS_PROCESS_RUNNING; service_loads(&state,&client);
    assert(state.job_kind==2 && !strcmp(spawned_url,"https://example.test/new/icon.png"));
    complete(&state,&client,"HTTP/1.1 307 Moved\r\nLocation: https://cdn.test/picture.png\r\n\r\n");
    assert(!image_decodes); follow(&state,&client);
    assert(!strcmp(spawned_url,"https://cdn.test/picture.png"));
    complete(&state,&client,"HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 3\r\n\r\nPNG");
    assert(image_decodes==1 && image_cache[state.active][0].decoded);
    assert(!strcmp(state.active_url,"https://example.test/new/index.html#section"));
    for (unsigned mode=0; mode<4; ++mode) {
        state=fresh(); assert(start_fetch(&state,"https://example.test/loop",1,0)==0);
        unsigned limit=mode==0 ? BROWSER_REDIRECT_LIMIT+1 : 1;
        for (unsigned hop=0; hop<limit; ++hop) {
            complete(&state,&client,"HTTP/1.1 302 Found\r\nLocation: /loop\r\n\r\n");
            if (hop+1<limit) follow(&state,&client);
        }
        if (mode==1) { clock_ms=state.redirect_deadline; service_loads(&state,&client); }
        if (mode==2) { handle_keyboard(&state,&client,BROWSER_KEY_ESCAPE); service_loads(&state,&client); }
        if (mode==3) {
            assert(navigate(&state,&client,"https://replacement.test/")==0);
            process.state=X86OS_PROCESS_RUNNING; service_loads(&state,&client);
            assert(!strcmp(spawned_url,"https://replacement.test/"));
            complete(&state,&client,"HTTP/1.1 500 Error\r\n\r\nfailed");
        }
        assert(!state.exit_requested && !state.follow_redirect && !state.child_pid && !exists);
        assert(!state.active && !strcmp(state.active_url,"/htdocs/index.html"));
        assert(spawns==(mode==0 ? BROWSER_REDIRECT_LIMIT+1 : mode==3 ? 2U : 1U));
    }
    const char *bad[] = {
        "HTTP/1.1 302 Found\r\nLocation: http://insecure.test/\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: file:///htdocs/index.html\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\npdf",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\ncompressed",
        "HTTP/1.1 404 Missing\r\n\r\nmissing", "truncated header"
    };
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        state=fresh(); assert(start_fetch(&state,"https://example.test/",1,0)==0);
        complete(&state,&client,bad[i]);
        assert(!state.active && !state.follow_redirect && !strcmp(state.active_url,"/htdocs/index.html"));
        assert(spawns==1 && waits==1 && !kills);
    }
}
int main(int argc, char **argv) {
    workspace = &host_workspace; file_length = strlen(file_bytes);
    failed_load(0, 0, 1); failed_load(1, 0, 1); failed_load(0, 1, 1);
    failed_load(0, 0, 2); failed_load(1, 0, 2);
    for (unsigned race = 0; race < 2; ++race) {
        browser_state_t state = fresh(); reist_gui_surface_client_t client = {0};
        assert(start_fetch(&state, "https://intracom.at/", 1, 0) == 0);
        service_loads(&state, &client); assert(!waits && !state.exit_requested);
        identity_exit = race; clock_ms += 10;
        process.exit_status = 0;
        if (!race) process.state = X86OS_PROCESS_ZOMBIE;
        service_loads(&state, &client);
        assert(!state.exit_requested && !state.child_pid && waits == 1);
        assert(state.active == 1 && !strcmp(state.active_url, "https://intracom.at/"));
        assert(opens == closes);
    }
    for (unsigned race = 0; race < 3; ++race) {
        browser_state_t state = fresh(); reist_gui_surface_client_t client = {0};
        assert(start_fetch(&state, "https://intracom.at/", 1, 0) == 0);
        if (race == 1) process.state = X86OS_PROCESS_ZOMBIE;
        kill_exit = race == 2;
        cancel_fetch(&state); cancel_fetch(&state); service_loads(&state, &client);
        assert(!state.exit_requested && !state.child_pid && waits == 1);
        assert(kills == (race == 1 ? 0U : 1U) && !state.active);
    }
    browser_state_t state = fresh(); reist_gui_surface_client_t client = {0};
    assert(start_fetch(&state, "https://intracom.at/", 1, 0) == 0);
    clock_ms = state.job_deadline; service_loads(&state, &client);
    assert(!state.exit_requested && waits == 1 && kills == 1 && !state.active);
    for (unsigned foreign = 0; foreign < 3; ++foreign) {
        state = fresh(); assert(start_fetch(&state, "https://intracom.at/", 1, 0) == 0);
        if (foreign == 0) ++generation;
        if (foreign == 1) { process.parent_pid = 99; process.state = X86OS_PROCESS_ZOMBIE; }
        if (foreign == 2) exists = 0;
        cancel_fetch(&state);
        assert(state.exit_requested && !kills && !waits);
    }
    redirect_cases();
    /* Optional captured public response: no network or test-fixture dependency. */
    if (argc == 2) {
        static char captured[BROWSER_DOCUMENT_LIMIT + 1];
        FILE *file = fopen(argv[1], "rb"); assert(file);
        file_length = fread(captured, 1, sizeof(captured), file); fclose(file);
        assert(file_length > 0 && file_length <= BROWSER_DOCUMENT_LIMIT);
        file_bytes = captured; state = fresh(); client.width = 800; client.height = 600;
        assert(publish_document(&state, &client, "/captured.html", "https://intracom.at/") == 0);
        assert(state.loaded && state.active == 1 && layouts[1].run_count > 0);
        printf("BROWSER_CAPTURED_HTML_OK bytes=%zu elements=%u runs=%u\n", file_length,
               documents[1].element_count, layouts[1].run_count);
    }
    puts("BROWSER_TRANSPORT_HOST_OK"); return 0;
}
'''


RENDER_HOST = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main static browser_application_main
#include "userspace/gui/apps/browser/main.c"
#undef main
#include "userspace/gui/compositor/desktop_surface.h"

static browser_workspace_t host_workspace;
static desktop_surface_manager_t manager;
static const reist_gui_surface_owner_t owner = {77, 23};
static unsigned live[2], generations[2], uploads, paint_failures;
static const uint32_t image_color = 0x00AB1234U;
void x86os_puts(const char *text) { fputs(text, stderr); }
void x86os_print_number(int number) { fprintf(stderr, "%d", number); }
int x86os_display_surface_buffer_create(uint32_t w, uint32_t h, const uint32_t *pixels,
                                        uint32_t stride, uint32_t *id, uint32_t *generation) {
    unsigned slot = live[0] ? 1U : 0U; assert(!live[slot]);
    assert(w && h && stride == w && pixels == surface_pixels);
    live[slot] = 1; *id = slot + 1; *generation = ++generations[slot]; ++uploads; return 0;
}
int x86os_display_surface_buffer_destroy(uint32_t id, uint32_t generation) {
    assert(id > 0 && id <= 2 && live[id - 1] && generations[id - 1] == generation);
    live[id - 1] = 0; return 0;
}
int reist_gui_surface_client_buffer_create(reist_gui_surface_client_t *c, const reist_gui_surface_buffer_t *b) {
    (void)c; return desktop_surface_buffer_create(&manager, owner, b);
}
int reist_gui_surface_client_buffer_destroy(reist_gui_surface_client_t *c, uint32_t id, uint32_t generation) {
    (void)c; return desktop_surface_buffer_destroy(&manager, owner, id, generation);
}
int reist_gui_surface_client_attach(reist_gui_surface_client_t *c, uint32_t id, uint32_t generation) {
    return desktop_surface_attach(&manager, owner, c->surface, id, generation, c->width, c->height);
}
int reist_gui_surface_client_damage(reist_gui_surface_client_t *c, reist_gui_rect_t r) {
    return desktop_surface_damage(&manager, owner, c->surface, r);
}
int reist_gui_surface_client_commit_with_release(reist_gui_surface_client_t *c, uint32_t *id, uint32_t *generation) {
    desktop_surface_commit_result_t result;
    int rc = desktop_surface_commit(&manager, owner, c->surface, &result);
    if (!rc) { *id = result.released_buffer_id; *generation = result.released_buffer_generation; }
    return rc;
}
int reist_gui_surface_client_paint_begin_layer(reist_gui_surface_client_t *c, uint32_t layer) {
    return desktop_surface_paint_begin_layer(&manager, owner, c->surface, layer);
}
int reist_gui_surface_client_paint_begin(reist_gui_surface_client_t *c) {
    return reist_gui_surface_client_paint_begin_layer(c, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
}
int reist_gui_surface_client_paint_commit_layer(reist_gui_surface_client_t *c, uint32_t layer) {
    return desktop_surface_paint_commit_layer(&manager, owner, c->surface, layer);
}
int reist_gui_surface_client_paint_commit(reist_gui_surface_client_t *c) {
    return reist_gui_surface_client_paint_commit_layer(c, REIST_GUI_SURFACE_PAINT_LAYER_BASE);
}
int reist_gui_surface_client_paint_fill(reist_gui_surface_client_t *c, reist_gui_rect_t r, uint32_t color) {
    int rc = desktop_surface_paint_fill(&manager, owner, c->surface, r, color);
    if (rc) { ++paint_failures; fprintf(stderr, "rejected fill x=%d y=%d w=%u h=%u\n", r.x,r.y,r.width,r.height); }
    return rc;
}
int reist_gui_surface_client_paint_font_text(reist_gui_surface_client_t *c, int32_t x, int32_t y,
    uint32_t width, const char *text, uint32_t length, uint32_t fg, uint32_t bg, uint32_t family, uint32_t height) {
    int rc = desktop_surface_paint_font_text(&manager, owner, c->surface,
        (reist_gui_rect_t){x,y,width,height}, fg,bg,text,length,family,height);
    if (rc) { ++paint_failures; fprintf(stderr, "rejected text x=%d y=%d w=%u h=%u bytes=%u\n", x,y,width,height,length); }
    return rc;
}
static void render_scroll_cases(const char *html, size_t length, uint32_t width, uint32_t height, unsigned decoded) {
    desktop_surface_initialize(&manager); memset(live, 0, sizeof(live));
    reist_gui_surface_client_t client = {0}; client.width=width; client.height=height;
    reist_gui_surface_configure_t configure;
    assert(desktop_surface_create(&manager, owner, REIST_GUI_SURFACE_ROLE_TOPLEVEL,
        width, height, &client.surface, &configure) == 0);
    assert(desktop_surface_ack_configure(&manager, owner, client.surface, configure.serial) == 0);
    browser_state_t state = {0}; state.loaded = 1; state.armed_link = UINT32_MAX;
    state.chrome_redraw = state.status_redraw = 1; set_status(&state, "Render test");
    assert(reist_html_document_parse((const uint8_t *)html, length, &documents[0]) == 0);
    for (unsigned i = 0; i < BROWSER_IMAGE_CACHE_COUNT; ++i) {
        browser_image_slot_t *slot = &image_cache[0][i]; slot->decoded = decoded;
        slot->width = slot->source_width = 2; slot->height = slot->source_height = 2;
        for (unsigned p = 0; p < 4; ++p) slot->pixels[p] = image_color;
    }
    assert(browser_build_layout(&documents[0], width, image_cache[0], &layouts[0]) == 0);
    uint32_t maximum = maximum_scroll(&state, &client);
    unsigned visible_images = 0;
    for (uint32_t y = 0; y <= maximum; ++y) {
        state.scroll_y = y; state.redraw = 1;
        if (render(&state, &client) != 0) {
            fprintf(stderr, "RENDER_REPRO_FAIL scroll=%u decoded=%u size=%ux%u\n", y,decoded,width,height);
            assert(0);
        }
        const desktop_surface_slot_t *surface = &manager.slots[client.surface.id - 1];
        assert(surface->committed && surface->committed_buffer == state.buffer_id);
        for (uint32_t h = 0; h < state.hit_count; ++h) {
            reist_gui_rect_t r = state.hits[h].rect;
            assert(r.x >= 0 && r.y >= (int32_t)BROWSER_CONTENT_TOP);
            assert((uint32_t)r.x + r.width <= width - BROWSER_SCROLLBAR_WIDTH);
            assert((uint32_t)r.y + r.height <= BROWSER_CONTENT_TOP + viewport_height(&client));
        }
        for (uint32_t i = 0; i < layouts[0].run_count; ++i) {
            browser_layout_run_t *run = &layouts[0].runs[i];
            if (!decoded || run->kind != REIST_HTML_ELEMENT_IMAGE || run->text_offset >= BROWSER_IMAGE_CACHE_COUNT) continue;
            int64_t top = (int64_t)BROWSER_CONTENT_TOP + run->y - y;
            int64_t bottom = top + run->height;
            if (top < BROWSER_CONTENT_TOP) top = BROWSER_CONTENT_TOP;
            if (bottom > BROWSER_CONTENT_TOP + viewport_height(&client)) bottom = BROWSER_CONTENT_TOP + viewport_height(&client);
            if (bottom <= top) continue;
            assert(surface_pixels[(size_t)top * width + run->x] == image_color);
            ++visible_images;
        }
    }
    if (decoded && documents[0].image_count) assert(visible_images);
    /* Chrome-only updates must not allocate/upload another document buffer. */
    unsigned before = uploads; state.chrome_redraw = 1;
    assert(render(&state, &client) == 0 && uploads == before);
    assert(desktop_surface_destroy(&manager, owner, client.surface) == 0);
    assert(x86os_display_surface_buffer_destroy(state.buffer_id, state.buffer_generation) == 0);
    assert(!live[0] && !live[1] && !paint_failures);
}
int main(int argc, char **argv) {
    workspace = &host_workspace;
    static const char html[] = "<p>Intro</p><a href='next.html'>"
        "<img src='missing.jpg' width='240' height='600' alt='abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL\xe2\x82\xac more'></a>"
        "<p><a href='next.html'>After the image</a></p>";
    for (unsigned decoded=0; decoded<2; ++decoded) {
        render_scroll_cases(html,sizeof(html)-1,800,600,decoded);
        render_scroll_cases(html,sizeof(html)-1,160,160,decoded);
    }
    if (argc == 2) {
        static char captured[BROWSER_DOCUMENT_LIMIT + 1]; FILE *file = fopen(argv[1], "rb"); assert(file);
        size_t length = fread(captured,1,sizeof(captured),file); fclose(file); assert(length<=BROWSER_DOCUMENT_LIMIT);
        render_scroll_cases(captured,length,800,600,0);
        render_scroll_cases(captured,length,800,600,1);
    }
    puts("BROWSER_RENDER_SCROLL_HOST_OK"); return 0;
}
'''


class BrowserRuntimeTests(unittest.TestCase):
    def test_real_renderer_scroll_clipping_and_image_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "browser-render-host.c"
            source.write_text(RENDER_HOST, encoding="utf-8")
            arguments = [os.environ["REIST_BROWSER_HTML_REPRO"]] if "REIST_BROWSER_HTML_REPRO" in os.environ else []
            run_host([str(source), "userspace/gui/apps/browser/browser_model.c",
                      "userspace/gui/lib/html_document.c", "userspace/gui/lib/value_controls.c",
                      "userspace/gui/compositor/desktop_surface.c", "userspace/gui/lib/font_catalog.c"],
                     arguments, ["-I.", "-Iuserspace/sdk/include", "-Iuserspace/storage/include",
                                 "-Wno-unused-function"])

    def test_real_transport_lifecycle_and_exit_races(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "browser-transport-host.c"
            source.write_text(TRANSPORT_HOST, encoding="utf-8")
            arguments = [os.environ["REIST_BROWSER_HTML_REPRO"]] if "REIST_BROWSER_HTML_REPRO" in os.environ else []
            run_host([str(source), "userspace/gui/apps/browser/browser_model.c",
                      "userspace/gui/lib/html_document.c", "userspace/gui/lib/value_controls.c",
                      "userspace/gui/apps/browser/browser_response.c", "userspace/programs/curl_http.c"],
                     arguments, ["-I.", "-Iuserspace/sdk/include", "-Iuserspace/storage/include",
                                 "-Wno-unused-function"])

    def test_typing_does_not_repaint_document(self):
        source = APP.read_text()
        chrome = source.split("static int render_chrome(")[1].split("static int render_status(")[0]
        for forbidden in ("render_body(", "publish_pixels(", "browser_build_layout(", "paint_begin(client)"):
            self.assertNotIn(forbidden, chrome)
        self.assertIn("REIST_GUI_SURFACE_PAINT_LAYER_HOVER", chrome)
        self.assertIn("processed < BROWSER_EVENT_BATCH_LIMIT", source)
        self.assertIn("state->body_frames != body", source)

    def test_child_is_bounded_and_generation_checked(self):
        source = APP.read_text()
        for contract in ("identity.generation != state->child_generation", "info.parent_pid == x86os_getpid()",
                         "X86OS_PROCESS_ZOMBIE", "BROWSER_FETCH_DEADLINE_MS", "BROWSER_PAGE_IMAGE_DEADLINE_MS",
                         "cancel_fetch", "reap_deadline", "BROWSER_IMAGE_CACHE_COUNT"):
            self.assertIn(contract, source)
        self.assertNotIn("fetch_network", source)
        self.assertIn("state->poll_at = x86os_uptime_ms()", source)
        self.assertNotIn("state->poll_at = 0U", source)

    def test_page_replacement_invalidates_old_hit_map(self):
        source = APP.read_text()
        publish = source.split("static int publish_document_bytes(")[1].split("static int publish_document(")[0]
        self.assertIn("state->hit_count = 0U", publish)
        self.assertIn("state->armed_link = UINT32_MAX", publish)

    def test_real_guest_requires_interaction_and_image_markers(self):
        source = APP.read_text()
        runner = (ROOT / "scripts/run_qemu_runtime_desktop.py").read_text()
        for marker in ("BROWSER_ADDRESS_CHROME_ONLY_OK", "BROWSER_IMAGE_PAINTED", "BROWSER_ANCHOR_OK",
                       "BROWSER_LINK_RELEASE_OK", "BROWSER_SCROLLBAR_CAPTURE_OK", "BROWSER_CLOSE_OK",
                       "BROWSER_TRANSPORT_EXIT_OK", "BROWSER_SCROLL_CLIP_OK"):
            self.assertIn(marker, source)
            self.assertIn(marker, runner)
        self.assertIn("click.x = -1", source)

    def test_fixture_is_packaged_in_both_layouts(self):
        for name in ("Makefile", "scripts/build-windows.ps1"):
            self.assertIn("browser-test.html", (ROOT / name).read_text())
        fixture = (ROOT / "htdocs/browser-test.html").read_text()
        self.assertIn('id="details"', fixture)
        self.assertIn("demo-colors.gif", fixture)


if __name__ == "__main__":
    unittest.main()
