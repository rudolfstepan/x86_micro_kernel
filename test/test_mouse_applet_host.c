#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main mouse_applet_main
#include "userspace/gui/apps/mouse/main.c"
#undef main
#include "reist/vfs_file_client.h"
static char disk[1024], pending[5][16];
static size_t cursor;
static uint64_t now;
static int open_status,spawn_status,alive,exit_code,generation,clock_status;
static unsigned spawns,waits,kills,fills,texts,reports;
static int paint_failure;
int x86os_monotonic_ms(uint64_t *out) { *out=now; return clock_status; }
int x86os_spawnv(const char *path,int argc,const char *const *argv) {
    assert(!strcmp(path,"/sbin/config.prg") && argc==13);
    assert(!strcmp(argv[1],"set") && !strcmp(argv[2],"input"));
    for (unsigned i=0;i<5;++i) { assert(!strcmp(argv[3+i*2],reist_mouse_keys[i])); strcpy(pending[i],argv[4+i*2]); }
    ++spawns; if (spawn_status<0) return spawn_status; alive=1; return 42;
}
int x86os_process_identity_of(int pid,x86os_process_identity_t *out) {
    assert(pid==42); *out=(x86os_process_identity_t){1,sizeof(*out),pid,(uint32_t)generation}; return alive ? 0 : -3;
}
int x86os_wait(int pid,int *status) { assert(pid==42 && !alive); ++waits; *status=exit_code; return pid; }
int x86os_kill(int pid) { assert(pid==42 && alive); ++kills; alive=0; exit_code=1; return 0; }
void x86os_puts(const char *s) { if (!strcmp(s,"MOUSE_DRAFT_READY")) ++reports; }
void x86os_putchar(char c) { (void)c; }
void x86os_print_number(int n) { (void)n; }
int x86os_sleep_ms(uint32_t ms) { now+=ms; return 0; }
int x86os_ipc_release(x86os_ipc_handle_t h) { (void)h; return 0; }
int reist_vfs_file_open_rights(const char *path,uint32_t timeout,uint32_t rights,reist_vfs_file_handle_t *out) {
    assert(!strcmp(path,"/etc/reist/input.conf") && timeout);
    assert(rights==(REIST_VFS_FILE_RIGHT_READ|REIST_VFS_FILE_RIGHT_STAT)); cursor=0; *out=1; return open_status;
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
int reist_gui_surface_client_create(reist_gui_surface_client_t *c,uint32_t r,uint32_t w,uint32_t h) { (void)c;(void)r;(void)w;(void)h;return 0; }
int reist_gui_surface_client_ack_configure(reist_gui_surface_client_t *c,uint32_t s) { (void)c;(void)s;return 0; }
int reist_gui_surface_client_accept_configure(reist_gui_surface_client_t *c,const reist_gui_surface_message_t *m) { (void)c;(void)m;return 0; }
int reist_gui_surface_client_set_title(reist_gui_surface_client_t *c,const char *t) { (void)c;(void)t;return 0; }
int reist_gui_surface_client_enable_scroll(reist_gui_surface_client_t *c) { (void)c;return 0; }
int reist_gui_surface_client_receive(reist_gui_surface_client_t *c,reist_gui_surface_message_t *m,uint32_t t) { (void)c;(void)m;(void)t;return -32; }
int reist_gui_surface_client_destroy(reist_gui_surface_client_t *c) { (void)c;return 0; }
int reist_gui_surface_client_paint_begin(reist_gui_surface_client_t *c) { (void)c;fills=texts=0;return 0; }
int reist_gui_surface_client_paint_commit(reist_gui_surface_client_t *c) { (void)c;assert(fills+texts<192);return paint_failure; }
int reist_gui_surface_client_paint_fill(reist_gui_surface_client_t *c,reist_gui_rect_t r,uint32_t color) {
    (void)color; assert(r.x>=0 && r.y>=0 && (uint32_t)r.x+r.width<=c->width && (uint32_t)r.y+r.height<=c->height); ++fills;return 0;
}
int reist_gui_surface_client_paint_text(reist_gui_surface_client_t *c,int32_t x,int32_t y,uint32_t w,const char *t,uint32_t n,uint32_t fg,uint32_t bg) {
    (void)fg;(void)bg;assert(x>=0 && y>=0 && (uint32_t)x+w<=c->width && n<=40 && strlen(t)>=n);++texts;return 0;
}
static void reset(void) {
    memset(&app,0,sizeof(app)); strcpy(disk,"schema=reist.input/1\nkeyboard.layout=de\n");
    open_status=spawn_status=alive=exit_code=clock_status=0; generation=7;
    spawns=waits=kills=reports=0; paint_failure=0; now=1;
    mouse_model_initialize(&app.model); app.client.width=620; app.client.height=452;
    app.test_label="Doppelklick hier testen"; assert(!layout());
}
static void key(uint32_t code) { reist_gui_surface_input_t e={.type=REIST_GUI_SURFACE_INPUT_KEYBOARD,.pressed=1,.key=code}; input(&e); }
static void click(int32_t x,int32_t y) {
    reist_gui_surface_input_t e={.type=REIST_GUI_SURFACE_INPUT_POINTER_BUTTON,.pressed=1,.button=1,.x=x,.y=y};
    input(&e); e.pressed=0; input(&e);
}
int main(void) {
    reset(); assert(app.model.writable && !paint());
    key(0x105); key(0x105); assert(app.model.draft.speed_percent==150);
    click(22,106); assert(app.model.draft.primary_right==1);
    click(230,163); assert(app.model.draft.acceleration==REIST_MOUSE_ADAPTIVE);
    click(22,205); assert(app.model.draft.natural_scroll==1);
    focus(6); key(0x105); assert(app.model.draft.double_click_ms==550);
    now=100; click(200,314); now=650; click(200,314); assert(!strcmp(app.test_label,"Doppelklick erkannt"));
    reist_gui_rect_t slider=app.ranges[0].bounds;
    click(slider.x,slider.y+10); assert(app.model.draft.speed_percent==25);
    click(slider.x+(int32_t)slider.width-1,slider.y+10); assert(app.model.draft.speed_percent==200);
    app.fault_probe=1; paint_failure=-5; assert(paint()!=0 && !reports);
    paint_failure=0; assert(!paint() && reports==1); assert(!paint() && reports==1);
    assert(!mouse_model_save(&app.model) && spawns==1); reist_mouse_settings_t frozen=app.model.pending;
    app.model.draft.speed_percent=50; assert(mouse_model_save(&app.model)==-16);
    assert(!mouse_model_poll(&app.model) && !waits);
    reist_config_document_t doc; assert(!reist_config_parse(disk,strlen(disk),"reist.input/1",&doc));
    for (unsigned i=0;i<5;++i) assert(!reist_config_set(&doc,reist_mouse_keys[i],pending[i]));
    size_t length=0; assert(!reist_config_serialize(&doc,disk,sizeof(disk),&length)); alive=0;
    assert(mouse_model_poll(&app.model) && waits==1 && !memcmp(&app.model.saved,&frozen,sizeof(frozen)));
    assert(app.model.draft.speed_percent==50 && !app.model.child);
    reset(); assert(!mouse_model_save(&app.model)); alive=0;
    /* Same defaults is a legitimate no-op save; mismatch must not succeed. */
    app.model.pending.speed_percent=150;
    assert(mouse_model_poll(&app.model) && strstr(app.model.status,"Nicht bestaetigt"));
    reset(); assert(!mouse_model_save(&app.model)); now=5001;
    assert(mouse_model_poll(&app.model) && kills==1 && !waits);
    assert(mouse_model_poll(&app.model) && waits==1 && !app.model.child);
    reset(); assert(!mouse_model_save(&app.model)); assert(!mouse_model_poll(&app.model)); generation=9;
    assert(mouse_model_poll(&app.model) && app.model.fatal && !kills && !waits);
    reset(); spawn_status=-13; assert(mouse_model_save(&app.model)==-13 && !app.model.child);
    reset(); open_status=-5; mouse_model_initialize(&app.model); assert(!app.model.writable);
    assert(mouse_model_save(&app.model)==-16 && !spawns);
    reset(); key(9); assert(app.focus==1 && app.control.focused==0);
    key(' '); assert(app.model.draft.primary_right);
    activate(8); assert(!app.model.draft.primary_right && app.model.draft.speed_percent==100);
    app.client.width=320; app.client.height=240; assert(!layout() && !app.usable && !paint());
    key(27); assert(app.close);
    puts("MOUSE_TEST_OK"); return 0;
}
