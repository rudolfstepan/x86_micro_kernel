#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main display_applet_main
#include "userspace/gui/apps/display/main.c"
#undef main
#include "reist/vfs_file_client.h"

static reist_display_mode_request_t fake_caps;
static char disk[512], pending[16];
static size_t cursor;
static uint64_t now;
static int query_status, open_status, spawn_status, alive, exit_code, generation;
static unsigned spawns, waits, kills, writes, fills, texts;
static unsigned selection_reports;
static int paint_failure;
int x86os_display_mode_query(reist_display_mode_request_t *out) { *out=fake_caps; return query_status; }
int x86os_monotonic_ms(uint64_t *out) { *out=now; return 0; }
int x86os_spawnv(const char *path,int argc,const char *const *argv) {
    assert(!strcmp(path,"/sbin/config.prg") && argc==5);
    assert(!strcmp(argv[1],"set") && !strcmp(argv[2],"desktop") && !strcmp(argv[3],"resolution"));
    ++spawns; strcpy(pending,argv[4]); if (spawn_status<0) return spawn_status;
    alive=1; return 42;
}
int x86os_process_identity_of(int pid,x86os_process_identity_t *out) {
    assert(pid==42); *out=(x86os_process_identity_t){1,sizeof(*out),pid,(uint32_t)generation};
    return alive ? 0 : -3;
}
int x86os_wait(int pid,int *status) { assert(pid==42 && !alive); ++waits; *status=exit_code; return pid; }
int x86os_kill(int pid) { assert(pid==42 && alive); ++kills; alive=0; exit_code=1; return 0; }
void x86os_puts(const char *s) {
    if (!strcmp(s,"DISPLAY_SELECTION_READY value=")) ++selection_reports;
}
void x86os_putchar(char c) { (void)c; }
int x86os_sleep_ms(uint32_t ms) { now+=ms; return 0; }
int x86os_ipc_release(x86os_ipc_handle_t h) { (void)h; return 0; }
int reist_vfs_file_open_rights(const char *path,uint32_t timeout,uint32_t rights,reist_vfs_file_handle_t *out) {
    assert(!strcmp(path,"/etc/reist/desktop.conf") && timeout);
    assert(rights==(REIST_VFS_FILE_RIGHT_READ|REIST_VFS_FILE_RIGHT_STAT));
    cursor=0; *out=1; return open_status;
}
int reist_vfs_file_fstat(reist_vfs_file_handle_t h,x86os_file_info_t *out) {
    assert(h==1); memset(out,0,sizeof(*out)); out->type=X86OS_FILE; out->size=(uint32_t)strlen(disk); return 0;
}
int reist_vfs_file_read(reist_vfs_file_handle_t h,void *out,size_t size) {
    assert(h==1); size_t n=strlen(disk)-cursor; if (n>size) n=size;
    memcpy(out,disk+cursor,n); cursor+=n; return (int)n;
}
int reist_vfs_file_close(reist_vfs_file_handle_t h) { assert(h==1); return 0; }
int reist_gui_surface_endpoint_from_argv(int n,char **v,x86os_ipc_handle_t *out) { (void)n;(void)v;(void)out;return -2; }
int reist_gui_surface_client_init(reist_gui_surface_client_t *c,x86os_ipc_handle_t e) { (void)c;(void)e;return 0; }
int reist_gui_surface_client_create(reist_gui_surface_client_t *c,uint32_t role,uint32_t w,uint32_t h) { (void)c;(void)role;(void)w;(void)h;return 0; }
int reist_gui_surface_client_ack_configure(reist_gui_surface_client_t *c,uint32_t s) { (void)c;(void)s;return 0; }
int reist_gui_surface_client_accept_configure(reist_gui_surface_client_t *c,const reist_gui_surface_message_t *m) { (void)c;(void)m;return 0; }
int reist_gui_surface_client_set_title(reist_gui_surface_client_t *c,const char *t) { (void)c;(void)t;return 0; }
int reist_gui_surface_client_enable_scroll(reist_gui_surface_client_t *c) { (void)c;return 0; }
int reist_gui_surface_client_receive(reist_gui_surface_client_t *c,reist_gui_surface_message_t *m,uint32_t t) { (void)c;(void)m;(void)t;return -32; }
int reist_gui_surface_client_destroy(reist_gui_surface_client_t *c) { (void)c;return 0; }
int reist_gui_surface_client_paint_begin(reist_gui_surface_client_t *c) { (void)c;fills=texts=0;return 0; }
int reist_gui_surface_client_paint_commit(reist_gui_surface_client_t *c) { (void)c;assert(fills<192);return paint_failure; }
int reist_gui_surface_client_paint_fill(reist_gui_surface_client_t *c,reist_gui_rect_t r,uint32_t color) {
    (void)color; assert(r.x>=0 && r.y>=0 && (uint32_t)r.x+r.width<=c->width && (uint32_t)r.y+r.height<=c->height);
    ++fills;return 0;
}
int reist_gui_surface_client_paint_text(reist_gui_surface_client_t *c,int32_t x,int32_t y,uint32_t w,const char *t,uint32_t n,uint32_t fg,uint32_t bg) {
    (void)fg;(void)bg;assert(x>=0 && y>=0 && (uint32_t)x+w<=c->width && n<=40 && strlen(t)>=n);++texts;return 0;
}
static void reset(void) {
    memset(&app,0,sizeof(app));
    fake_caps=(reist_display_mode_request_t){.version=1,.struct_size=64,.operation=13,
        .backend=REIST_DISPLAY_BACKEND_DISPI,.max_width=4096,.max_height=4096,
        .scanout_bytes=16U*1024U*1024U,.shadow_bytes=16U*1024U*1024U,
        .width=1024,.height=768,.bpp=32,.flags=REIST_DISPLAY_MODE_ACTIVE};
    strcpy(disk,"schema=reist.desktop/1\ntheme=classic\nfuture.key=kept\nresolution=auto\n");
    query_status=open_status=spawn_status=alive=exit_code=0;generation=7;
    spawns=waits=kills=writes=selection_reports=0;paint_failure=0;now=1;
    display_model_initialize(&app.model);app.client.width=620;app.client.height=452;
    assert(layout()==0);
}
static void finish_save(int correct) {
    alive=0;
    if (correct) { snprintf(disk,sizeof(disk),"schema=reist.desktop/1\ntheme=classic\nfuture.key=kept\nresolution=%s\n",pending);++writes; }
}
static void key(uint32_t code) {
    reist_gui_surface_input_t event={.type=REIST_GUI_SURFACE_INPUT_KEYBOARD,.pressed=1,.key=code};input(&event);
}
static void button(int32_t x,int32_t y,uint32_t pressed) {
    reist_gui_surface_input_t event={.type=REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,.button=1,.pressed=pressed,.x=x,.y=y};input(&event);
}
int main(void) {
    reset();assert(render()==0 && !selection_reports);
    app.fault_probe=1;paint_failure=-11;
    assert(render()==-11 && !selection_reports && !app.selection_reported);
    paint_failure=0;assert(render()==0 && selection_reports==1);
    assert(render()==0 && selection_reports==1);
    key(0x103);assert(render()==0 && selection_reports==2);
    assert(app.reported_selection==1U);
    reset();assert(app.model.writable && app.model.count<=DISPLAY_CHOICE_CAPACITY);
    assert(render()==0 && fills>20 && texts>10);
    key(0x103);assert(app.model.selected==1 && app.model.choices[1].width==800);
    button(40,390,1);assert(!spawns);button(40,390,0);assert(spawns==1 && app.model.child==42);
    assert(display_model_save(&app.model)==-16 && spawns==1);
    assert(display_model_poll(&app.model)==0 && waits==0);
    assert(app.model.caps.width==1024 && app.model.saved_width==0);
    finish_save(1);assert(display_model_poll(&app.model)==1 && waits==1 && writes==1);
    assert(app.model.saved_width==800 && app.model.caps.width==1024 && strstr(disk,"future.key=kept"));
    key(0x101);assert(app.close_requested && !kills);
    /* A restarted 800x600 session must retain every queued Down selection. */
    reset();strcpy(disk,"schema=reist.desktop/1\nresolution=800x600\n");
    display_model_initialize(&app.model);assert(layout()==0 && app.model.selected==1U);
    for (unsigned step=0U;step<3U;++step) {
        key(0x103);assert(app.model.selected==step+2U);assert(render()==0);
    }
    button(50,395,1);button(50,395,0);
    assert(spawns==1U && !strcmp(pending,"1280x720"));
    finish_save(1);assert(display_model_poll(&app.model)==1);
    assert(app.model.saved_width==1280 && app.model.saved_height==720);
    reset();app.model.selected=4;assert(display_model_save(&app.model)==0);
    finish_save(0);assert(display_model_poll(&app.model)==1 && !app.model.saved_width);
    reset();app.model.selected=1;assert(display_model_save(&app.model)==0);
    now+=DISPLAY_SAVE_DEADLINE_MS;assert(display_model_poll(&app.model)==1 && kills==1 && waits==0);
    assert(display_model_poll(&app.model)==1 && waits==1 && !app.model.saved_width);
    reset();assert(display_model_save(&app.model)==0);assert(display_model_poll(&app.model)==0);
    generation=8;assert(display_model_cancel(&app.model)==-13 && !kills && !waits);
    reset();spawn_status=-13;assert(display_model_save(&app.model)==-13 && !app.model.child);
    open_status=-13;display_model_initialize(&app.model);assert(!app.model.writable && !app.model.child);
    reset();strcpy(disk,"schema=reist.desktop/1\nresolution=4096x4096\n");
    display_model_initialize(&app.model);assert(app.model.writable && !app.model.selected);
    assert(app.model.saved_width==4096 && strstr(app.model.status,"nicht verfuegbar"));
    reset();app.client.width=360;app.client.height=260;assert(!layout() && !render());
    app.client.width=200;assert(!layout() && !app.usable);
    reset();fake_caps.backend=REIST_DISPLAY_BACKEND_VBE;
    fake_caps.fixed_width=1024;fake_caps.fixed_height=768;
    display_model_initialize(&app.model);assert(app.model.count==2 && app.model.choices[1].width==1024);
    fake_caps.reserved2=1;display_model_initialize(&app.model);assert(!app.model.writable);
    puts("DISPLAY_TEST_OK");return 0;
}
