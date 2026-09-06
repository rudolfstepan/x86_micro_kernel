import ast
import os
import re
import types
import tempfile
import unittest
import inspect
import json
import sys
from unittest import mock
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
static unsigned exists, generation, identity_exit, kill_exit, waits, kills, clock_ms, clock_reads;
static unsigned kill_pending, peer_closed;
static const char *file_bytes = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<title>Downloaded</title><p>Working page</p>";
static size_t file_length, file_offset;
static unsigned opens, closes;
static unsigned bulk_calls;
static unsigned spawns, unlinks, image_decodes;
static char spawned_url[BROWSER_URL_CAPACITY];
static browser_workspace_t host_workspace;
static browser_html_reply_t worker_reply;
static uint8_t worker_wire[BROWSER_CSS_WIRE_CAPACITY];
static unsigned request_written, worker_length, worker_offset, endpoint_live;
static browser_scene_t worker_scene;
int x86os_ipc_create(x86os_ipc_handle_t *h) { assert(!endpoint_live); *h=42; endpoint_live=1; request_written=worker_length=worker_offset=0; return 0; }
int x86os_ipc_close(x86os_ipc_handle_t h) { assert(h==42 && endpoint_live); endpoint_live=0; return 0; }
int x86os_ipc_delegate(x86os_ipc_handle_t h,int pid,uint32_t rights) { assert(h==42 && endpoint_live && pid==81 && rights==3);return 0; }
int x86os_ipc_send_bulk_timeout(x86os_ipc_handle_t h,const x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    assert(h==42 && endpoint_live && !timeout && m->length>16);
    browser_css_packet_t p; memcpy(&p,m->payload,sizeof(p));
    assert(p.offset==request_written && p.total==css_input.request.header.size);
    assert(!memcmp(p.bytes,(uint8_t *)&css_input+request_written,m->length-16));
    request_written+=m->length-16; return 0;
}
int x86os_ipc_receive_bulk_timeout(x86os_ipc_handle_t h,x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    assert(h==42 && endpoint_live && !timeout);
    if(peer_closed) return -32;
    /* A reaped peer's last queued packet remains readable, then the real
     * kernel reports EPIPE, not EAGAIN. Never confuse stream EOF with OOM. */
    if(worker_offset==worker_length) return worker_length ? -32 : -11;
    uint32_t n=worker_length-worker_offset; if(n>BROWSER_CSS_PACKET_DATA)n=BROWSER_CSS_PACKET_DATA;
    browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,css_input.request.header.request,worker_offset,worker_length,{0}};
    memcpy(p.bytes,worker_wire+worker_offset,n); memcpy(m->payload,&p,16+n); m->length=16+n; worker_offset+=n;
    if (worker_offset==worker_length) { process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=0; }
    return 0;
}

int x86os_getpid(void) { return 77; }
int x86os_create(const char *path) { (void)path; assert(0); return -5; }
int x86os_write(int fd,const void *bytes,size_t size) { (void)fd; (void)bytes; (void)size; assert(0); return -5; }
int x86os_close(int fd) { (void)fd; assert(0); return -5; }
uint32_t x86os_uptime_ms(void) { ++clock_reads; return clock_ms; }
int x86os_unlink(const char *path) { assert(path[0] && !exists); ++unlinks; return 0; }
int x86os_spawnv(const char *path, int argc, const char *const *argv) {
    if (!strcmp(path,"/usr/bin/htmlwork.prg")) {
        assert(argc==3 && !exists && endpoint_live && !request_written);
        assert(!strcmp(argv[1],"--ipc") && !strcmp(argv[2],"42"));
        exists=1; ++spawns; return process.pid;
    }
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
    if (pid==77) { *identity=(x86os_process_identity_t){1,sizeof(*identity),77,19}; return 0; }
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
    ++kills;
    /* task_exit_status revokes IPC before publishing the zombie. During
     * cleanup process_terminate refuses a second termination request. */
    if(kill_pending) return -1;
    process.state = X86OS_PROCESS_ZOMBIE;
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
    assert(capacity<=131072); ++bulk_calls;
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
    kill_pending=peer_closed=0;
    spawns = unlinks = image_decodes = 0;
    clock_ms = 100; state.loaded = 1; state.image_next = BROWSER_IMAGE_CACHE_COUNT;
    endpoint_live=0; memset(scenes,0,sizeof(scenes));
    browser_resources_init(&workspace->resources[0],1);
    browser_resources_init(&workspace->resources[1],1);
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
static void fault_cleanup_cases(void) {
    reist_gui_surface_client_t client={.width=800,.height=600};
    for(unsigned variant=0;variant<4;++variant) {
        unsigned stuck=variant&1;
        browser_state_t state=fresh();
        if(variant>=2) clock_ms=UINT32_MAX-100;
        exists=endpoint_live=kill_pending=peer_closed=1;
        state.child_pid=81; state.child_generation=generation;
        state.job_kind=3; state.css_endpoint=42;
        state.poll_at=clock_ms; /* Same monotone origin as real spawn. */
        css_input.request.header.size=state.css_sent=444;
        state.job_deadline=clock_ms+5000;
        /* Exact fault schedule: peer revoked, still live, duplicate kill
         * rejected. Keep the old page and the same pinned child, no retry. */
        service_loads(&state,&client);
        assert(!state.exit_requested && state.child_pid==81 && exists);
        assert(state.job_cancelled && !state.css_endpoint && !endpoint_live);
        assert(kills==1 && !waits && state.loaded && !state.active);
        clock_ms+=10; service_loads(&state,&client);
        assert(!state.exit_requested && kills==1 && !waits);
        if(stuck) {
            clock_ms+=1000; service_loads(&state,&client);
            assert(state.exit_requested && state.exit_error==-110 && kills==1 && !waits);
            assert(!strcmp(state.exit_reason,"cancel-reap-timeout"));
        } else {
            process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=134;
            clock_ms+=10; service_loads(&state,&client);
            assert(!state.exit_requested && !state.child_pid && !exists);
            assert(waits==1 && kills==1 && state.parser_failures==1);
            assert(!strcmp(state.active_url,"/htdocs/index.html"));
            assert(!navigate(&state,&client,"/htdocs/browser-html5-test.html") && state.pending);
        }
    }
}
static void complete(browser_state_t *state, reist_gui_surface_client_t *client, const char *response) {
    assert(exists); file_bytes=response; file_length=strlen(response);
    clock_ms+=10; process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=0;
    service_loads(state,client);
    assert(!exists && !state->child_pid && !state->exit_requested && opens==closes);
}
/* The child boundary supplies a semantic reply; the real upstream parser and
 * worker I/O are exercised separately in test_html_engine.py. */
static void complete_html(browser_state_t *state, reist_gui_surface_client_t *client, unsigned corrupt) {
    assert(state->parse_pending && !exists);
    unsigned old_unlinks=unlinks;
    if (!client->width) { client->width=800; client->height=600; }
    uint32_t length=state->parse_pending;
    process.state=X86OS_PROCESS_RUNNING; service_loads(state,client);
    assert(exists && state->job_kind==3 && state->child_generation==generation && !state->parse_pending);
    for(unsigned i=0;i<40 && state->css_sent<css_input.request.header.size;++i) assert(!service_css_ipc(state));
    assert(request_written==css_input.request.header.size);
    memset(&worker_reply,0,sizeof(worker_reply)); worker_reply.header=state->html_request;
    worker_reply.header.size=sizeof(worker_reply); worker_reply.header.child_pid=81;
    worker_reply.header.child_generation=generation;
    assert(!reist_html_document_parse(document_bytes,length,&worker_reply.document));
    static browser_layout_t layout;
    assert(!browser_build_layout(&worker_reply.document,client->width,NULL,&layout));
    memset(&worker_scene,0,sizeof(worker_scene)); worker_scene.version=BROWSER_SCENE_VERSION;
    worker_scene.width=css_input.request.width; worker_scene.height=css_input.request.height;
    worker_scene.total_height=layout.total_height;
    for(unsigned i=0;i<layout.run_count;++i) {
        browser_layout_run_t *r=&layout.runs[i];
        if(r->kind!=1 && r->kind!=5 && r->kind!=6) continue;
        worker_scene.runs[worker_scene.count++]=(browser_scene_run_t){r->kind,r->text_offset,r->text_length,r->link_index,
            r->x,r->y,r->width,r->height,0xff202020,r->style&67};
    }
    if (corrupt) ++worker_reply.header.request;
    int packed=browser_css_pack(&worker_reply,&worker_scene,worker_wire,sizeof(worker_wire)); assert(packed>0);
    worker_length=(unsigned)packed; worker_offset=0;
    for(unsigned i=0;i<40 && state->css_received<worker_length;++i) assert(!service_css_ipc(state));
    clock_ms+=10; process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=0;
    service_loads(state,client);
    assert(!exists && !state->child_pid && !state->exit_requested && opens==closes);
    assert(!endpoint_live);
    assert(unlinks==old_unlinks); /* CSS IPC never creates temporary files. */
}
static void follow(browser_state_t *state, reist_gui_surface_client_t *client) {
    assert(state->follow_redirect && !exists);
    unsigned before=unlinks;
    process.state=X86OS_PROCESS_RUNNING; service_loads(state,client);
    assert(exists && state->child_pid && !state->follow_redirect && unlinks==before);
}
static void wheel_cases(void) {
    browser_state_t state=fresh();
    reist_gui_surface_client_t client={.width=800,.height=600};
    layouts[state.active].total_height=4000;
    state.address_focused=1; state.redraw=0; state.armed_link=0; state.hit_count=3;
    reist_gui_surface_input_t input={.type=REIST_GUI_SURFACE_INPUT_POINTER_SCROLL,
        .serial=1,.x=20,.y=BROWSER_CONTENT_TOP+20,.delta_y=120};
    handle_pointer(&state,&client,&input);
    assert(state.scroll_y==48 && state.redraw && state.address_focused && !state.hit_count);
    assert(state.armed_link==UINT32_MAX && !spawns && !unlinks && !state.pending);
    input.delta_y=-120; handle_pointer(&state,&client,&input); assert(!state.scroll_y);
    state.redraw=0; handle_pointer(&state,&client,&input); assert(!state.scroll_y && !state.redraw);
    input.delta_y=1;
    for(unsigned i=0;i<120;++i) handle_pointer(&state,&client,&input);
    assert(state.scroll_y==48);
    input.delta_y=INT32_MAX; handle_pointer(&state,&client,&input);
    assert(state.scroll_y==maximum_scroll(&state,&client));
    input.delta_y=INT32_MIN; handle_pointer(&state,&client,&input); assert(!state.scroll_y);
    input.delta_y=120; input.y=20; handle_pointer(&state,&client,&input); assert(!state.scroll_y);
    input.y=BROWSER_CONTENT_TOP+20; state.scrollbar.state.captured=1;
    handle_pointer(&state,&client,&input); assert(!state.scroll_y);
    state.scrollbar.state.captured=0; input.pressed=1;
    handle_pointer(&state,&client,&input); assert(!state.scroll_y);
    /* Selection cannot alter normal browsing or the ordinary late wheel
     * assertions; one initial upward detent is the trusted probe selector. */
    input.pressed=0; input.delta_y=-120; state.address_focused=0;
    assert(!resource_probe_selected(&state,&input));
    state.probe=1; assert(resource_probe_selected(&state,&input));
    state.probe_phase=10; assert(!resource_probe_selected(&state,&input));
    state.probe_phase=0; state.address_focused=1;
    assert(!resource_probe_selected(&state,&input));
    state.address_focused=0; input.delta_y=120;
    assert(!resource_probe_selected(&state,&input));
    input.delta_y=-120; state.probe=2;
    assert(!resource_probe_selected(&state,&input));
}
/* Discovery reply comes from an untrusted worker. Drive the real coordinator
 * through reaping, immutable bundle assembly, redirects and cancellation. */
static void complete_needs(browser_state_t *state,reist_gui_surface_client_t *client,const char *url,unsigned corrupt) {
    assert(state->parse_pending && !exists);
    process.state=X86OS_PROCESS_RUNNING; service_loads(state,client);
    assert(state->child_pid && state->job_kind==3);
    for(unsigned i=0;i<100 && state->css_sent<css_input.request.header.size;++i) assert(!service_css_ipc(state));
    assert(state->css_sent==css_input.request.header.size);
    browser_resource_needs_t n={.magic=BROWSER_RESOURCE_NEED_MAGIC,.version=BROWSER_RESOURCE_VERSION,
        .generation=workspace->resources[state->active^1U].generation,.identity=state->html_request};
    n.identity.child_pid=81; n.identity.child_generation=generation;
    assert(!browser_resource_need_add(&n,url,1));
    n.size=offsetof(browser_resource_needs_t,items)+sizeof(n.items[0]);
    if(corrupt) ++n.generation;
    memcpy(worker_wire,&n,n.size); worker_length=n.size; worker_offset=0;
    for(unsigned i=0;i<40 && state->css_received<worker_length;++i) assert(!service_css_ipc(state));
    clock_ms+=10; process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=0;
    service_loads(state,client);
    assert(!exists && !endpoint_live && !state->child_pid);
}
static void resource_cases(void) {
    const char *css="HTTP/1.1 200 OK\r\nContent-Type: text/css\r\n\r\np {color:green}";
    reist_gui_surface_client_t client={.width=800,.height=600};
    for(unsigned mode=0;mode<8;++mode) {
        browser_state_t state=fresh();
        const char *url="https://example.test/page.html", *sheet="https://example.test/style.css";
        memcpy(document_bytes,"<p>safe</p>",11);
        assert(!publish_document_bytes(&state,&client,url,11));
        uint32_t deadline=state.resource_deadline;
        complete_needs(&state,&client,sheet,mode==1);
        if(mode==1) { assert(!state.resource_loading && state.parser_failures==1); continue; }
        assert(state.resource_loading && !state.active && state.loaded && waits==1);
        browser_resources_t *b=&workspace->resources[1]; assert(b->count==1 && !b->entries[0].ready);
        if(mode==2) {
            clock_ms=deadline; service_loads(&state,&client);
            assert(!state.resource_loading && spawns==1 && !b->entries[0].ready); continue;
        }
        process.state=X86OS_PROCESS_RUNNING; service_loads(&state,&client);
        assert(state.child_pid && state.job_kind==4 && spawns==2 && !strcmp(spawned_url,sheet));
        assert((int32_t)(state.job_deadline-deadline)<=0);
        if(mode==3 || mode==4) {
            if(mode==3) handle_keyboard(&state,&client,BROWSER_KEY_ESCAPE);
            else assert(!navigate(&state,&client,"/replacement.html"));
            service_loads(&state,&client);
            assert(!state.resource_loading && !state.child_pid && kills==1 && waits==2 && !state.active);
            assert(!state.parse_pending && !b->entries[0].ready); continue;
        }
        if(mode==5) {
            complete(&state,&client,"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nwrong");
            assert(!state.resource_loading && !b->entries[0].ready && !state.active); continue;
        }
        if(mode==6) {
            complete(&state,&client,"HTTP/1.1 302 Found\r\nLocation: http://insecure.test/a.css\r\n\r\n");
            assert(!state.resource_loading && !state.follow_redirect && !b->entries[0].ready); continue;
        }
        if(mode==7) {
            complete(&state,&client,"HTTP/1.1 302 Found\r\nLocation: /fresh.css\r\n\r\n");
            follow(&state,&client); assert(!strcmp(spawned_url,"https://example.test/fresh.css"));
        }
        complete(&state,&client,css);
        assert(b->entries[0].ready && !state.active && state.resource_loading && state.resource_deadline==deadline);
        service_loads(&state,&client); assert(!state.resource_loading && state.parse_pending==11);
        complete_html(&state,&client,0); assert(state.active==1 && !strcmp(state.active_url,url));
        uint32_t old_generation=b->generation;
        /* Same fresh HTML cannot reuse a scene whose external CSS may change. */
        assert(!publish_document_bytes(&state,&client,url,11));
        assert(state.parse_pending==11 && workspace->resources[0].generation>old_generation && !workspace->resources[0].count);
        assert(b->count==1 && b->entries[0].ready); /* previous page remains immutable */
    }
    /* Zero-length local sheets are legitimate and have a paired close. */
    browser_state_t state=fresh(); memcpy(document_bytes,"<p>safe</p>",11);
    assert(!publish_document_bytes(&state,&client,"/page.html",11));
    complete_needs(&state,&client,"/empty.css",0);
    file_bytes=""; file_length=0; service_loads(&state,&client);
    assert(workspace->resources[1].entries[0].ready && !workspace->resources[1].length && opens==closes);
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
    assert(spawns==1 && waits==1 && unlinks==2 && state.redirect_count==1);
    assert(!strcmp(state.job_url,"https://example.test/new/index.html#section"));
    assert(!strcmp(state.address,"https://typed.test/"));
    follow(&state,&client);
    assert(state.redirect_deadline==deadline && !strcmp(spawned_url,"https://example.test/new/index.html"));
    complete(&state,&client,"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<p id='section'><a href='next.html'>Next</a><img src='icon.png'></p>");
    assert(!state.active && state.parse_pending);
    complete_html(&state,&client,0);
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
    /* Preserve the first fatal decision across later cleanup errors. A
     * non-EIO failure must not turn into a successful process exit. */
    browser_state_t diagnostic={0};
    browser_runtime_failure(&diagnostic,"test-receive",-84);
    browser_runtime_failure(&diagnostic,"test-cleanup",-9);
    assert(diagnostic.exit_requested && diagnostic.exit_error==-84);
    assert(!strcmp(diagnostic.exit_reason,"test-receive"));
    assert(browser_runtime_result(&diagnostic,0)==1);
    assert(browser_runtime_result(&(browser_state_t){0},-11)==0);
    assert(browser_runtime_result(&(browser_state_t){0},-5)==1);
    /* Disabled counters add no clock syscalls. Enabled counters remain bounded
     * across uptime wrap and saturated totals, without changing any deadline. */
    clock_reads=0; assert(!timing_start()); timing_end(TIME_READ,0);
    assert(!clock_reads && !timings[TIME_READ].calls);
    timing_enabled=1; clock_ms=UINT32_MAX-4; uint32_t started=timing_start();
    clock_ms=3; timing_end(TIME_READ,started);
    assert(timings[TIME_READ].total==8 && timings[TIME_READ].maximum==8);
    timings[TIME_READ].total=INT32_MAX-1; clock_ms=5; timing_end(TIME_READ,3);
    assert(timings[TIME_READ].total==INT32_MAX && timings[TIME_READ].calls==2);
    timing_end(TIME_COUNT,0); timing_enabled=0; memset(timings,0,sizeof(timings));
    static const char bulk_file[214860]={1,2,3};
    const char *saved_file=file_bytes; size_t saved_length=file_length;
    file_bytes=bulk_file; file_length=sizeof(bulk_file); uint32_t bulk_length=0;
    assert(!read_file("/large.gif",image_bytes,sizeof(image_bytes),&bulk_length));
    assert(bulk_calls==2 && bulk_length==sizeof(bulk_file) && !memcmp(image_bytes,bulk_file,bulk_length));
    file_bytes=saved_file; file_length=saved_length;
    browser_state_t clean=fresh();
    cleanup_fetch_files(&clean); cleanup_fetch_files(&clean);
    assert(!unlinks); /* No speculative VFS work at startup or after cleanup. */
    wheel_cases();
    static uint32_t font_data[13];
    font_data[0]=REIST_GUI_FONT_PSF2_MAGIC; font_data[2]=32; font_data[3]=1; font_data[4]=1;
    font_data[5]=16; font_data[6]=16; font_data[7]=8;
    ((uint8_t *)font_data)[48]='?'; ((uint8_t *)font_data)[49]=255;
    assert(!reist_gui_font_open_psf2(&workspace->font,(uint8_t *)font_data,50,workspace->font_map,262144,'?'));
    /* EOF before the declared end still fails closed; only a complete stream
     * stops receiving. Do not broadly ignore EPIPE from an incomplete child. */
    browser_state_t truncated=fresh(); truncated.css_endpoint=42; endpoint_live=1;
    truncated.css_sent=css_input.request.header.size;
    worker_offset=worker_length=32; truncated.css_total=32; truncated.css_received=31;
    assert(service_css_ipc(&truncated)==-84 && truncated.css_received==31);
    endpoint_live=0;
    failed_load(0, 0, 1); failed_load(1, 0, 1); failed_load(0, 1, 1);
    failed_load(0, 0, 2); failed_load(1, 0, 2);
    fault_cleanup_cases();
    for (unsigned race = 0; race < 2; ++race) {
        browser_state_t state = fresh(); reist_gui_surface_client_t client = {0};
        assert(start_fetch(&state, "https://intracom.at/", 1, 0) == 0);
        service_loads(&state, &client); assert(!waits && !state.exit_requested);
        identity_exit = race; clock_ms += 10;
        process.exit_status = 0;
        if (!race) process.state = X86OS_PROCESS_ZOMBIE;
        service_loads(&state, &client);
        assert(!state.exit_requested && !state.child_pid && waits == 1);
        complete_html(&state,&client,0);
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
    resource_cases();
    /* Exact document/URL/viewport reuse must not start another parser. Reload
     * still refreshes images; changed bytes, origin, geometry or fault mode do. */
    state=fresh(); client.width=800; client.height=600;
    memcpy(document_bytes,"<p>safe</p>",11);
    assert(!publish_document_bytes(&state,&client,"/cache.html",11));
    complete_html(&state,&client,0);
    unsigned cached_spawns=spawns;
    assert(!publish_document_bytes(&state,&client,"/cache.html",11));
    assert(!state.parse_pending && spawns==cached_spawns && !state.child_pid);
    /* A real fragment navigation then reload must refresh the file, not start
     * another identical CSS generation just because active_url has an anchor. */
    assert(!navigate(&state,&client,"#details"));
    assert(!strcmp(state.active_url,"/cache.html#details"));
    file_bytes="<p>safe</p>"; file_length=11;
    unsigned cached_opens=opens;
    handle_keyboard(&state,&client,'r'); assert(state.pending);
    service_loads(&state,&client);
    assert(opens==cached_opens+1 && opens==closes);
    assert(!state.pending && !state.parse_pending && !state.reflow_pending && spawns==cached_spawns);
    assert(!strcmp(state.active_url,"/cache.html") && !strcmp(state.address,"/cache.html"));
    assert(!publish_document_bytes(&state,&client,"/cache.html#another",11));
    assert(!state.parse_pending && !strcmp(state.active_url,"/cache.html#another"));
    assert(!publish_document_bytes(&state,&client,"/cache.html?changed=1",11) && state.parse_pending);
    state.parse_pending=0;
    assert(!publish_document_bytes(&state,&client,"https://other.test/cache.html",11) && state.parse_pending);
    state.parse_pending=0;
    assert(!publish_document_bytes(&state,&client,"/cache.html",11) && !state.parse_pending);
    state.parse_mode=1;
    assert(!publish_document_bytes(&state,&client,"/cache.html",11) && state.parse_pending);
    state.parse_mode=state.parse_pending=0;
    assert(!publish_document_bytes(&state,&client,"/other.html",11) && state.parse_pending);
    state.parse_pending=0; ++client.width;
    assert(!publish_document_bytes(&state,&client,"/cache.html",11) && state.parse_pending);
    state.parse_pending=0; --client.width; document_bytes[3]='S';
    assert(!publish_document_bytes(&state,&client,"/cache.html",11) && state.parse_pending);
    state.parse_pending=0; document_bytes[3]='s';
    documents[state.active].images[0].width=documents[state.active].images[0].height=0;
    memcpy(image_bytes,"PNG",3); state.reflow_pending=0;
    state.scene_image_sizes[0][0]=state.scene_image_sizes[0][1]=1;
    assert(!load_image_bytes(&state,&client,0,3) && !state.reflow_pending);
    state.scene_image_sizes[0][0]=0;
    assert(!load_image_bytes(&state,&client,0,3) && state.reflow_pending);
    for (unsigned failure=0; failure<4; ++failure) {
        state=fresh(); client.width=800; client.height=600;
        memcpy(document_bytes,"<p>safe</p>",11);
        assert(!publish_document_bytes(&state,&client,"/new.html",11));
        if (!failure) complete_html(&state,&client,1); /* stale reply */
        else {
            service_loads(&state,&client); assert(state.child_pid>0 && state.job_kind==3);
            if (failure==1) { process.state=X86OS_PROCESS_ZOMBIE; process.exit_status=70; }
            if (failure==2) clock_ms=state.job_deadline;
            if (failure==3) handle_keyboard(&state,&client,BROWSER_KEY_ESCAPE);
            service_loads(&state,&client);
        }
        assert(!state.active && !state.exit_requested && !state.child_pid && waits==1);
        assert(state.parser_failures==1 && state.parser_timeouts==(failure==2));
        assert(!publish_document_bytes(&state,&client,"/next.html",11));
        complete_html(&state,&client,0);
        assert(state.active==1 && !strcmp(state.active_url,"/next.html") && waits==2);
    }
    /* Optional captured public response: no network or test-fixture dependency. */
    if (argc == 2) {
        static char captured[BROWSER_DOCUMENT_LIMIT + 1];
        FILE *file = fopen(argv[1], "rb"); assert(file);
        file_length = fread(captured, 1, sizeof(captured), file); fclose(file);
        assert(file_length > 0 && file_length <= BROWSER_DOCUMENT_LIMIT);
        file_bytes = captured; state = fresh(); client.width = 800; client.height = 600;
        assert(publish_document(&state, &client, "/captured.html", "https://intracom.at/") == 0);
        complete_html(&state,&client,0);
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
uint32_t x86os_uptime_ms(void) { return 0; }
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


RASTER_CACHE_HOST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/apps/browser/browser_scene.h"
static unsigned raster_calls;
static int counted_raster(const reist_gui_font_t *f, uint32_t g, uint32_t w, uint32_t h,
    uint32_t fg, uint32_t bg, uint32_t *p, uint32_t stride, size_t capacity) {
    ++raster_calls;
    return reist_gui_font_raster_scaled_xrgb(f,g,w,h,fg,bg,p,stride,capacity);
}
#define reist_gui_font_raster_scaled_xrgb counted_raster
#include "userspace/gui/apps/browser/browser_scene.c"
#undef reist_gui_font_raster_scaled_xrgb
static reist_html_document_t doc;
static browser_scene_t scene;
static uint32_t pixels[320*256], first[320*256];
int main(void) {
    /* Valid PSF metadata and a real mapping/raster; no fake glyph generator. */
    uint8_t bits[16]; memset(bits,0xa5,sizeof(bits));
    reist_gui_font_mapping_t mapping={65,0};
    reist_gui_font_t font={.version=REIST_GUI_FONT_API_VERSION,.struct_size=sizeof(font),
        .data=bits,.data_size=sizeof(bits),.glyph_count=1,.width=8,.height=16,
        .row_bytes=1,.bytes_per_glyph=16,.mappings=&mapping,.mapping_capacity=1};
    const char html[]="<p>AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA</p>";
    assert(!reist_html_document_parse((const uint8_t *)html,sizeof(html)-1,&doc));
    assert(doc.text_length>=32);
    scene=(browser_scene_t){.version=BROWSER_SCENE_VERSION,.width=320,.height=256,.total_height=256,.count=8};
    for (unsigned i=0;i<8;++i) scene.runs[i]=(browser_scene_run_t){
        .kind=1,.length=32,.link=UINT32_MAX,.y=(int32_t)i*24,.width=256,.height=16,.color=0xff123456};
    assert(!browser_scene_raster(&doc,&scene,&font,NULL,0,pixels,320,256,0,256));
    printf("GLYPH_RASTER_CALLS actual=%u repeated=256\n",raster_calls);
    assert(raster_calls==1); /* All 256 equal glyph/size pairs share preparation. */
    for (unsigned y=0;y<256;++y) for (unsigned x=0;x<320;++x) {
        unsigned row=y/24, gy=y%24;
        uint32_t expected=row<8 && gy<16 && x<256 && (0xa5U&(0x80U>>(x%8))) ? 0x123456 : 0;
        assert(pixels[y*320+x]==expected);
    }
    memcpy(first,pixels,sizeof(first));
    memset(pixels,0,sizeof(pixels)); raster_calls=0;
    assert(!browser_scene_raster(&doc,&scene,&font,NULL,0,pixels,320,256,0,256));
    assert(raster_calls==1 && !memcmp(first,pixels,sizeof(first)));
    /* A new invocation must not reuse a prior font's bytes or success. */
    memset(bits,0,sizeof(bits)); memset(pixels,0,sizeof(pixels));
    assert(!browser_scene_raster(&doc,&scene,&font,NULL,0,pixels,320,256,0,256));
    for(unsigned i=0;i<320*256;++i) assert(!pixels[i]);
    font.data_size=1;
    assert(browser_scene_raster(&doc,&scene,&font,NULL,0,pixels,320,256,0,256)==-84);
    font.data_size=sizeof(bits); memset(bits,0xff,sizeof(bits));
    scene.runs[1].height=20; scene.runs[1].width=320;
    raster_calls=0;
    assert(!browser_scene_raster(&doc,&scene,&font,NULL,13,pixels,320,256,0,256));
    assert(raster_calls==2); /* Size is part of the key; clipped runs are safe. */
    puts("BROWSER_GLYPH_CACHE_HOST_OK");
    return 0;
}
'''


class BrowserRuntimeTests(unittest.TestCase):
    def test_input_probe_keeps_overall_deadline_across_both_sessions(self):
        # Execute the real runner's deadline assignments and dispatch, with a
        # late first startup. No replacement deadline model or guest sleep.
        tree = ast.parse((ROOT / "scripts/run_qemu_runtime_desktop.py").read_text())
        run = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "run")
        assignments = [n for n in ast.walk(run) if isinstance(n, ast.Assign)
                       and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name)
                       and n.targets[0].id in ("overall_deadline", "desktop_deadline")]
        start = next(n for n in run.body if isinstance(n, ast.Assign)
                     and isinstance(n.targets[0], ast.Name) and n.targets[0].id == "deadline")
        bounded = next(n for n in ast.walk(run) if isinstance(n, ast.Assign)
                       and isinstance(n.targets[0], ast.Name) and n.targets[0].id == "deadline"
                       and isinstance(n.value, ast.IfExp))
        dispatch = next(n for n in ast.walk(run) if isinstance(n, ast.If)
                        and isinstance(n.test, ast.Name) and n.test.id == "browser_input_probe")
        for now in (60.0, 170.0, 205.0):
            for selected in (False, True):
                clock = mock.Mock(side_effect=(10.0, now))
                capture = mock.Mock(return_value=0)
                namespace = dict(time=types.SimpleNamespace(monotonic=clock), timeout=180.0,
                    font_catalog_start=True, browser_input_probe=selected,
                    process=None, output=None, transcript=[], screenshot=None, browser_input=None,
                    run_browser_input_probe=capture)
                body = [start, *sorted(assignments, key=lambda n:n.lineno), bounded,
                        ast.parse("first_deadline = deadline").body[0]]
                exec(compile(ast.fix_missing_locations(ast.Module(body=body,type_ignores=[])),
                             "real-input-deadlines", "exec"), namespace)
                self.assertEqual(namespace["overall_deadline"], 190.0)
                self.assertEqual(namespace["first_deadline"], min(190.0,now+90.0) if selected else now+90.0)
                if selected:
                    function = ast.parse("def dispatch_probe():\n    pass\n")
                    function.body[0].body = dispatch.body
                    exec(compile(ast.fix_missing_locations(function), "real-input-dispatch", "exec"), namespace)
                    namespace["dispatch_probe"]()
                    self.assertEqual(capture.call_args.args[4], 190.0)
                    self.assertEqual(clock.call_count, 2) # no renewal at dispatch/relaunch

    def test_input_probe_requires_real_edits_and_never_synthesizes_them(self):
        from test_memory_r12 import function_block
        source = APP.read_text()
        probe = function_block(source, "static int input_probe_step(")
        self.assertNotIn("handle_keyboard(", probe)
        self.assertIn("state->input_backspace == 1U", probe)
        self.assertIn("state->input_left == 1U", probe)
        self.assertIn("state->input_right == 1U", probe)
        self.assertIn('"/htdocs/index.html"', probe)
        self.assertIn("BROWSER_INPUT_NAVIGATION_OK", probe)
        self.assertIn("state->image_next < images", probe)
        self.assertIn("state->reflow_pending", probe)

    def test_native_keyboard_probe_sends_balanced_modifier_events(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        monitor = desktop.BrowserInputMonitor(None, 1)
        monitor.execute = mock.Mock()
        with mock.patch.object(desktop.time, "sleep"):
            monitor.key("shift-semicolon")
            events = monitor.execute.call_args.args[1]["events"]
            self.assertEqual([(e["data"]["key"]["data"],e["data"]["down"]) for e in events],
                             [("shift",True),("semicolon",True),("semicolon",False),("shift",False)])
            for bad in ("ctrl-alt-delete", "", "reset", "left;quit"):
                with self.assertRaises(ValueError): monitor.key(bad)
        monitor.execute.assert_called_once()

    @staticmethod
    def qmp_socket(*messages):
        peer = mock.Mock()
        peer.recv.side_effect = [message if isinstance(message, bytes) else
                                json.dumps(message).encode() + b"\r\n"
                                for message in messages] + [b""]
        return peer

    def test_browser_qmp_input_is_acknowledged_without_mux_sleeps(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        peer = self.qmp_socket(b'{"QMP":', b'{"version":{},"capabilities":[]}}\r\n',
                               {"return": {}, "id": 1}, {"event": "RESUME"},
                               *({"return": {}, "id": i} for i in range(2, 7)))
        with mock.patch.object(desktop.time, "monotonic", return_value=0), \
             mock.patch.object(desktop.time, "sleep", side_effect=AssertionError("fixed mux sleep")):
            monitor = desktop.BrowserInputMonitor(peer, 30)
            monitor.negotiate()
            for command in ("mouse_move -64 -32", "mouse_button 1", "mouse_button 0",
                            "mouse_move 0 0 -1", "mouse_move 0 0 1"):
                monitor.mouse(None, command)
        requests = [json.loads(call.args[0]) for call in peer.sendall.call_args_list]
        self.assertEqual(requests[0]["execute"], "qmp_capabilities")
        self.assertEqual([r["id"] for r in requests], list(range(1, 7)))
        self.assertTrue(all(r["execute"] == "input-send-event" for r in requests[1:]))
        self.assertEqual(requests[1]["arguments"]["events"], [
            {"type": "rel", "data": {"axis": "x", "value": -64}},
            {"type": "rel", "data": {"axis": "y", "value": -32}}])
        for index, button, down in ((2, "left", True), (3, "left", False),
                                     (4, "wheel-down", True), (5, "wheel-up", True)):
            events = requests[index]["arguments"]["events"]
            self.assertEqual(events[0], {"type": "btn", "data": {"button": button, "down": down}})
            if index >= 4:
                self.assertEqual(events[1], {"type": "btn", "data": {"button": button, "down": False}})
        self.assertTrue(all(0 < c.args[0] <= 5 for c in peer.settimeout.call_args_list))

    def test_browser_qmp_rejects_bad_replies_and_bounds_work(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        cases = [[b""], [b"invalid\r\n"], [[1]], [{"return": {}, "id": 99}],
                 [{"return": {}, "id": True}], [{"error": {"class": "GenericError"}, "id": 1}],
                 [b"x" * 65536], [{"event": "RESUME"}] * 33]
        for replies in cases:
            with self.subTest(replies=str(replies)[:80]), \
                 mock.patch.object(desktop.time, "monotonic", return_value=0):
                peer = self.qmp_socket(*replies)
                monitor = desktop.BrowserInputMonitor(peer, 30)
                with self.assertRaises(RuntimeError):
                    monitor.execute("qmp_capabilities", {})
                self.assertLessEqual(peer.recv.call_count, 32)
        peer = self.qmp_socket()
        monitor = desktop.BrowserInputMonitor(peer, 0)
        with self.assertRaises(TimeoutError):
            monitor.mouse(None, "mouse_button 1")
        peer.sendall.assert_not_called()
        for command in ("quit", "mouse_move 121 0", "mouse_move 0 0 2", "mouse_button 7",
                        "mouse_move 1 2\nquit"):
            with self.subTest(command=command), self.assertRaises(RuntimeError):
                monitor.mouse(None, command)
        peer.sendall.assert_not_called()

    def test_browser_qmp_failed_admission_closes_peer(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        for greeting in ({"not-qmp": {}}, b"", b"{bad}\r\n"):
            peer = self.qmp_socket(greeting)
            listener = mock.Mock()
            listener.accept.return_value = (peer, ("127.0.0.1", 12345))
            with mock.patch.object(desktop.time, "monotonic", return_value=0), \
                 self.assertRaises(RuntimeError):
                desktop.BrowserInputMonitor.accept(listener, 30)
            peer.close.assert_called_once_with()

    def test_browser_qmp_failure_reaps_guest_and_listener(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        child = mock.Mock()
        child.poll.return_value = None
        peer = self.qmp_socket(b"")
        listener = mock.Mock()
        listener.accept.return_value = (peer, ("127.0.0.1", 12345))
        arguments = {name: False for name in inspect.signature(desktop.run).parameters}
        with tempfile.TemporaryDirectory() as directory:
            arguments.update(qemu=Path("qemu.exe"), image=Path("disk.img"),
                             screenshot=Path(directory) / "browser.ppm", timeout=30,
                             metrics_log=None, smp=1, browser_probe=True, vmware_vga=True)
            with mock.patch.object(desktop.subprocess, "Popen", return_value=child) as spawn, \
                 mock.patch.object(desktop, "open_injection_listener", return_value=(listener, 4321)), \
                 mock.patch.object(desktop, "configure_qemu_host_timers", return_value=False), \
                 mock.patch.object(desktop.threading, "Thread"):
                with self.assertRaisesRegex(RuntimeError, "peer closed"):
                    desktop.run(**arguments)
            self.assertIn("tcp:127.0.0.1:4321,server=off,nodelay=on", spawn.call_args.args[0])
            self.assertTrue((Path(directory) / "browser.browser.log").exists())
        listener.close.assert_called_once_with()
        peer.close.assert_called_once_with()
        child.terminate.assert_called_once_with()
        child.wait.assert_called_once_with(timeout=3)

    def test_repeated_glyph_preparation_is_bounded_and_frame_local(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "browser-glyph-cache-host.c"
            source.write_text(RASTER_CACHE_HOST, encoding="utf-8")
            run_host([str(source), "userspace/gui/apps/browser/html_protocol.c",
                      "userspace/gui/apps/browser/browser_forms.c",
                      "userspace/gui/lib/html_document.c", "userspace/gui/lib/font.c"],
                     flags=["-I.", "-Iuserspace/sdk/include"])

    def test_wheel_probe_targets_nearest_document_point_after_real_resize(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        width, height = 1024, 768
        client_width, client_height = 800, 600
        origin = (100, 50)
        scene_width = client_width - 18
        box_x = (scene_width - (scene_width // 2 + 30)) // 2
        pixels = bytearray(b"\xff" * (width * height * 3))
        at = ((origin[1] + 76 + 12 + 3) * width + origin[0] + box_x + 3) * 3
        pixels[at:at+9] = b"\xe0\xf0\xff\x12\x34\x56\x22\x44\x88"
        commands = []
        observed = []
        input_monitor = mock.Mock(side_effect=lambda _, c: commands.append(c))
        input_monitor.mouse.side_effect = lambda _, c: commands.append(c)
        def observe(_, path, point, stage):
            if stage == "home":
                self.assertEqual(point, (0, 0))
                self.assertTrue(all(c == "mouse_move -120 -120" for c in commands))
            else:
                self.assertEqual(stage, "grip")
                self.assertEqual(point, (origin[0] + client_width - 1, origin[1] + client_height - 1))
                self.assertNotIn("mouse_button 1", commands)
            observed.append(stage)
        with mock.patch.object(desktop, "capture_screenshot"), \
             mock.patch.object(desktop, "browser_probe_wait_pointer", side_effect=observe, create=True), \
             mock.patch.object(desktop, "read_ppm", return_value=(width, height, pixels)), \
             mock.patch.object(desktop.time, "monotonic", return_value=0), \
             mock.patch.object(desktop.time, "sleep"), \
             mock.patch.object(desktop, "qemu_monitor_command", side_effect=lambda _, c: commands.append(c)):
            pointer, target = desktop.browser_probe_css_resize(None, Path("browser.ppm"), 30, client_width, client_height, input_monitor)
            self.assertEqual(observed, ["home", "grip"], "wire ACK cannot replace guest consumption")
            self.assertIn("mouse_button 1", commands)
            self.assertIn("mouse_move -64 -32", commands)
            self.assertEqual(commands[-1], "mouse_button 0")
            self.assertGreaterEqual(target[0], origin[0])
            self.assertLess(target[0], origin[0] + client_width - 64 - 18)
            self.assertGreaterEqual(target[1], origin[1] + 76)
            self.assertLess(target[1], origin[1] + client_height - 32 - 22)
            commands.clear()
            desktop.shortcut_probe_move_mouse(None, pointer, *target)
            self.assertEqual(len(commands), 1, "wheel target must not traverse the entire document")
            self.assertEqual(pointer, list(target))

    @staticmethod
    def pointer_frame(point=(0, 0)):
        # Build the observed pixels from the production pointer, not the
        # matcher constant. Include the real one-pixel 0x606060 shadow.
        text = (ROOT / "drivers/video/framebuffer.c").read_text()
        shape = re.findall(r'"([BW.]+)"', text.split("static const char pointer_shape")[1].split("};")[0])
        width, height = 96, 72
        pixels = bytearray(b"\x20\x40\x60" * (width * height))
        for y in range(19):
            for x in range(14):
                color = shape[y][x] if y < len(shape) and x < len(shape[0]) else "."
                rgb = {"B": b"\0\0\0", "W": b"\xff\xff\xff"}.get(color)
                if rgb is None and x and y and shape[y-1][x-1] in "BW":
                    rgb = b"\x60\x60\x60"
                if rgb is not None:
                    offset = ((point[1] + y) * width + point[0] + x) * 3
                    pixels[offset:offset+3] = rgb
        return width, height, pixels

    def test_pointer_scanout_match_rejects_wrong_or_corrupt_pixels(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        for point in ((0, 0), (71, 42)):
            frame = self.pointer_frame(point)
            self.assertTrue(desktop.browser_probe_pointer_at(frame, point))
            self.assertFalse(desktop.browser_probe_pointer_at(frame, (point[0]+1, point[1])))
            frame[2][(point[1] * frame[0] + point[0]) * 3] = 1
            self.assertFalse(desktop.browser_probe_pointer_at(frame, point))
        for frame, point in ((None, (0, 0)), ((96, 72, b""), (0, 0)),
                             (self.pointer_frame(), (-1, 0)), (self.pointer_frame(), (95, 71))):
            self.assertFalse(desktop.browser_probe_pointer_at(frame, point))

    def test_pointer_wait_requires_fresh_scanout_and_is_bounded(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        monitor = types.SimpleNamespace(deadline=30, execute=mock.Mock())
        with mock.patch.object(desktop.time, "monotonic", return_value=0), \
             mock.patch.object(desktop.time, "sleep"), \
             mock.patch.object(desktop, "read_ppm", side_effect=[self.pointer_frame((20, 10)), self.pointer_frame()]):
            desktop.browser_probe_wait_pointer(monitor, Path("browser.ppm"), (0, 0), "home")
        self.assertEqual(monitor.execute.call_count, 2)
        calls = monitor.execute.call_args_list
        self.assertTrue(all(c.args[0] == "screendump" and c.args[2] <= 1 for c in calls))
        self.assertNotEqual(calls[0].args[1]["filename"], calls[1].args[1]["filename"])
        monitor.execute.reset_mock()
        with mock.patch.object(desktop.time, "monotonic", return_value=0), \
             mock.patch.object(desktop.time, "sleep"), \
             mock.patch.object(desktop, "read_ppm", return_value=self.pointer_frame((20, 10))), \
             self.assertRaisesRegex(RuntimeError, "pointer.*home"):
            desktop.browser_probe_wait_pointer(monitor, Path("browser.ppm"), (0, 0), "home")
        self.assertEqual(monitor.execute.call_count, 16)
        monitor.execute.side_effect = RuntimeError("no capture ACK")
        with mock.patch.object(desktop, "read_ppm") as read, self.assertRaisesRegex(RuntimeError, "no capture ACK"):
            monitor.deadline = float("inf")
            desktop.browser_probe_wait_pointer(monitor, Path("browser.ppm"), (0, 0), "home")
        read.assert_not_called()

    def test_desktop_runner_reaps_on_timer_policy_failure(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        child = mock.Mock()
        child.poll.return_value = None
        listener = mock.Mock()
        arguments = {name: False for name in inspect.signature(desktop.run).parameters}
        arguments.update(qemu=Path("qemu.exe"), image=Path("disk.img"),
                         screenshot=Path("browser.ppm"), timeout=1,
                         metrics_log=None, smp=1, browser_probe=True, vmware_vga=True)
        with mock.patch.object(desktop.subprocess, "Popen", return_value=child), \
             mock.patch.object(desktop, "open_injection_listener", return_value=(listener, 4321)), \
             mock.patch.object(desktop, "configure_qemu_host_timers", create=True,
                               side_effect=OSError("timer policy refused")) as policy, \
             mock.patch.object(desktop.threading, "Thread",
                               side_effect=AssertionError("reader started before timer admission")):
            with self.assertRaisesRegex(OSError, "timer policy refused"):
                desktop.run(**arguments)
        policy.assert_called_once_with(child)
        listener.close.assert_called_once_with()
        child.terminate.assert_called_once_with()
        child.wait.assert_called_once_with(timeout=3)
        child.stdin.close.assert_called_once_with()
        child.stdout.close.assert_called_once_with()

    def run_browser_probe_transcript(self, chunks, events=None):
        # Execute the actual guest-runner branch. Only time, QEMU output and
        # screenshot I/O are mocked, so late failure/close ordering is real.
        tree = ast.parse((ROOT / "scripts/run_qemu_runtime_desktop.py").read_text())
        branch = next(node for node in ast.walk(tree)
                      if isinstance(node, ast.If) and isinstance(node.test, ast.Name)
                      and node.test.id == "browser_probe"
                      and any(isinstance(item, ast.Constant) and item.value == "Browser probe failed"
                              for item in ast.walk(node)))
        required = next(ast.literal_eval(node.value) for node in ast.walk(branch)
                        if isinstance(node, ast.Assign)
                        and any(isinstance(target, ast.Name) and target.id == "required"
                                for target in node.targets))
        pending = iter(chunk.replace("ALL_REQUIRED", "\n".join(required)) for chunk in chunks)
        ticks = iter(range(100))
        captures = []
        if events is None:
            events = []
        def resize(*args):
            events.append("resize")
            return [900, 600], (874, 570)
        namespace = dict(re=re,time=types.SimpleNamespace(monotonic=lambda: next(ticks), sleep=lambda _: None),
                         deadline=20, transcript=[], process=None, output=None, screenshot=None,
                         drain=lambda output, transcript: transcript.append(next(pending, "")),
                         browser_probe_css_resize=resize,
                         shortcut_probe_move_mouse=lambda *args, **kwargs: events.append("move-target"),
                         browser_input=types.SimpleNamespace(mouse=lambda _, command: events.append(command)),
                         qemu_monitor_command=lambda _, command: events.append(command),
                         capture_screenshot=lambda *args: captures.append(True), print=lambda *args: None)
        function = ast.parse("def run():\n    pass\n")
        function.body[0].body = branch.body
        exec(compile(ast.fix_missing_locations(function), "real-browser-probe", "exec"), namespace)
        return namespace["run"](), captures

    def test_guest_runner_wheel_direction_and_ack_order(self):
        events = []
        def chunks():
            yield "BROWSER_CSS_RESIZE_WAIT width=800 height=600"
            yield "BROWSER_WHEEL_WAIT"
            yield "BROWSER_WHEEL_WAIT"
            self.assertEqual(events, ["resize", "move-target", "mouse_move 0 0 -1"])
            yield "BROWSER_WHEEL_DOWN_OK"
            yield "BROWSER_WHEEL_DOWN_OK"
            yield "ALL_REQUIRED"
            yield "BROWSER_CLOSE_OK"
        result, _ = self.run_browser_probe_transcript(chunks(), events)
        self.assertEqual(result, 0)
        # QEMU hmp_mouse_move: dz>0 is WHEEL_UP, dz<0 is WHEEL_DOWN.
        # Repeated markers must not replay a wheel; up waits for down's ACK.
        self.assertEqual(events, ["resize", "move-target",
                                  "mouse_move 0 0 -1", "mouse_move 0 0 1"])

    def test_guest_runner_rejects_late_failure(self):
        for failure in ("BROWSER_PROBE_FAIL interaction", "BROWSER_PROBE_FAIL cleanup", "DESKTOP_BROWSER_FAIL"):
            with self.subTest(failure=failure), self.assertRaisesRegex(RuntimeError, "Browser probe failed"):
                self.run_browser_probe_transcript(["ALL_REQUIRED", failure + "\nBROWSER_CLOSE_OK"])

    def test_guest_runner_requires_close_and_preserves_screenshot(self):
        result, captures = self.run_browser_probe_transcript(["ALL_REQUIRED", "BROWSER_CLOSE_OK"])
        self.assertEqual(result, 0)
        self.assertEqual(captures, [True])
        with self.assertRaisesRegex(RuntimeError, "close cleanly"):
            self.run_browser_probe_transcript(["ALL_REQUIRED"])

    def test_real_renderer_scroll_clipping_and_image_pixels(self):
        # Original renderer proof stays unchanged; resource lifecycle is a
        # separate real coordinator/worker and guest verification boundary.
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "browser-render-host.c"
            source.write_text(RENDER_HOST, encoding="utf-8")
            arguments = [os.environ["REIST_BROWSER_HTML_REPRO"]] if "REIST_BROWSER_HTML_REPRO" in os.environ else []
            run_host([str(source), "userspace/gui/apps/browser/browser_model.c",
                      "userspace/gui/lib/html_document.c", "userspace/gui/lib/value_controls.c",
                      "userspace/gui/compositor/desktop_surface.c", "userspace/gui/lib/font_catalog.c",
                      "userspace/gui/apps/browser/browser_scene.c", "userspace/gui/apps/browser/browser_forms.c", "userspace/gui/apps/browser/html_protocol.c", "userspace/gui/lib/font.c"],
                     arguments, ["-I.", "-Iuserspace/sdk/include", "-Iuserspace/storage/include",
                                 "-Wno-unused-function"])

    def test_real_transport_lifecycle_and_exit_races(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "browser-transport-host.c"
            source.write_text(TRANSPORT_HOST, encoding="utf-8")
            arguments = [os.environ["REIST_BROWSER_HTML_REPRO"]] if "REIST_BROWSER_HTML_REPRO" in os.environ else []
            run_host([str(source), "userspace/gui/apps/browser/browser_model.c",
                      "userspace/gui/lib/html_document.c", "userspace/gui/lib/value_controls.c",
                      "userspace/gui/apps/browser/browser_response.c", "userspace/programs/curl_http.c",
                      "userspace/gui/apps/browser/browser_resources.c", "userspace/gui/apps/browser/browser_forms.c",
                      "userspace/gui/apps/browser/html_protocol.c", "userspace/gui/apps/browser/browser_scene.c", "userspace/gui/lib/font.c"],
                     arguments, ["-I.", "-Iuserspace/sdk/include", "-Iuserspace/storage/include",
                                 "-Wno-unused-function"])

    def test_resource_guest_requires_ordered_cleanup_and_close(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        markers = ["DESKTOP_BROWSER_OK\nBROWSER_PROBE_SELECTOR_READY", "BROWSER_RESOURCES_STARTED", "BROWSER_RESOURCES_CASCADE_PIXELS_OK",
                   "BROWSER_RESOURCES_DEDUPE_CYCLE_OK", "BROWSER_RESOURCES_FAILURE_CONTAINED_OK",
                   "BROWSER_RESOURCES_CANCEL_OK", "BROWSER_RESOURCES_RELOAD_FRESH_OK",
                   "BROWSER_RESOURCES_RECOVERY_OK", "BROWSER_RESOURCES_CLEANUP_OK", "BROWSER_CLOSE_OK"]
        for case in (markers, markers[:-1], markers[:-2]+markers[-1:],
                     markers[:-1]+["BROWSER_PROBE_FAIL cleanup",markers[-1]],
                     markers[:3]+list(reversed(markers[3:]))):
            with self.subTest(case=case), mock.patch.object(desktop.time, "monotonic", side_effect=range(30)), \
                 mock.patch.object(desktop.time, "sleep"), mock.patch.object(desktop, "send_key") as key, \
                 mock.patch.object(desktop, "capture_screenshot") as capture, \
                 mock.patch.object(desktop, "drain", side_effect=lambda _, text: text.extend(case)):
                monitor = mock.Mock()
                if case == markers:
                    self.assertEqual(desktop.run_browser_resource_probe(None,None,[],None,20,monitor),0)
                    monitor.mouse.assert_called_once_with(None,"mouse_move 0 0 1")
                    capture.assert_called_once()
                else:
                    with self.assertRaises(RuntimeError):
                        desktop.run_browser_resource_probe(None,None,[],None,20,monitor)
                key.assert_not_called()

    def test_resource_selector_waits_for_both_peers_and_guest_ack(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import run_qemu_runtime_desktop as desktop
        for first, second in (("DESKTOP_BROWSER_OK", "BROWSER_PROBE_SELECTOR_READY"),
                              ("BROWSER_PROBE_SELECTOR_READY", "DESKTOP_BROWSER_OK")):
            monitor = mock.Mock()
            def drain(_, transcript):
                if not transcript:
                    transcript.append(first)
                elif len(transcript)==1:
                    monitor.mouse.assert_not_called()
                    transcript.append(second)
                else:
                    monitor.mouse.assert_called_once_with(None,"mouse_move 0 0 1")
            with mock.patch.object(desktop, "drain", side_effect=drain), \
                 mock.patch.object(desktop.time, "monotonic", side_effect=range(30)), \
                 mock.patch.object(desktop.time, "sleep"):
                with self.assertRaisesRegex(RuntimeError, "deadline.*BROWSER_RESOURCES_STARTED"):
                    desktop.run_browser_resource_probe(None,None,[],None,20,monitor)

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
        publish = source.split("static int publish_html_reply(")[1].split("static int publish_document_bytes(")[0]
        self.assertIn("state->hit_count = 0U", publish)
        self.assertIn("state->armed_link = UINT32_MAX", publish)

    def test_real_guest_requires_interaction_and_image_markers(self):
        source = APP.read_text()
        runner = (ROOT / "scripts/run_qemu_runtime_desktop.py").read_text()
        for marker in ("BROWSER_ADDRESS_CHROME_ONLY_OK", "BROWSER_IMAGE_PAINTED", "BROWSER_ANCHOR_OK",
                       "BROWSER_LINK_RELEASE_OK", "BROWSER_SCROLLBAR_CAPTURE_OK", "BROWSER_CLOSE_OK",
                       "BROWSER_TRANSPORT_EXIT_OK", "BROWSER_SCROLL_CLIP_OK", "BROWSER_HTML5_WORKER_OK",
                       "BROWSER_HTML5_FAULT_CONTAINED_OK", "BROWSER_HTML5_TIMEOUT_CONTAINED_OK", "BROWSER_HTML5_RECOVERY_OK"):
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
