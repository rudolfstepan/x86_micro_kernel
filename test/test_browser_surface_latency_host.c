#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
@REAL@

static browser_workspace_t arena;
static browser_state_t state;
static reist_gui_surface_client_t client;
static uint32_t live[2], generations[2], committed, registered[2];
static unsigned calls, fail_at, raster_fail, malformed_release;
static reist_gui_rect_t damage;
static uint32_t old_pixels[REIST_GUI_SURFACE_MAX_BUFFER_BYTES/4U];
static unsigned allocation_fail,allocations;
void *x86os_realloc(void *p,size_t n) {
    assert(n<=REIST_GUI_SURFACE_MAX_BUFFER_BYTES); ++allocations;
    return allocation_fail ? NULL : realloc(p,n);
}
static unsigned old_width, old_height;
static int fault(void) { return ++calls == fail_at ? -5 : 0; }
uint32_t x86os_uptime_ms(void) { return 1; }
void x86os_puts(const char *s) { (void)s; }
void x86os_print_number(int n) { (void)n; }
int browser_scene_raster_forms(const reist_html_document_t *d, const browser_scene_t *s,
    const reist_gui_font_t *f, const browser_image_slot_t *i, const browser_form_state_t *v,
    uint32_t scroll, uint32_t *p, uint32_t w, uint32_t h, uint32_t top, uint32_t view) {
    (void)d;(void)s;(void)f;(void)i;(void)v;
    assert(top + view <= h);
    if (raster_fail) return -27;
    for (uint32_t y=top;y<top+view;++y)
        for (uint32_t x=0;x<w;++x) p[y*w+x]=(y+scroll)*1000+x;
    return 0;
}
int x86os_display_surface_buffer_create(uint32_t w,uint32_t h,const uint32_t *p,
    uint32_t stride,uint32_t *id,uint32_t *generation) {
    assert(w==client.width && h==client.height && stride==w && p==surface_pixels);
    int rc=fault(); if(rc) return rc;
    unsigned slot=live[0] ? 1 : 0; assert(!live[slot]);
    live[slot]=1; *id=slot+1; *generation=++generations[slot]; return 0;
}
int x86os_display_surface_buffer_destroy(uint32_t id,uint32_t generation) {
    assert(id>=1 && id<=2 && live[id-1] && generation==generations[id-1]);
    assert(!registered[id-1]); live[id-1]=0; return 0;
}
int reist_gui_surface_client_buffer_create(reist_gui_surface_client_t *c,const reist_gui_surface_buffer_t *b) {
    assert(c==&client && b->width==c->width && b->height==c->height);
    assert(b->capability_generation==generations[b->capability_id-1]);
    int rc=fault(); if(!rc) registered[b->capability_id-1]=1; return rc;
}
int reist_gui_surface_client_buffer_destroy(reist_gui_surface_client_t *c,uint32_t id,uint32_t generation) {
    assert(c==&client && registered[id-1] && generation==generations[id-1]);
    int rc=fault(); if(!rc) registered[id-1]=0; return rc;
}
int reist_gui_surface_client_attach(reist_gui_surface_client_t *c,uint32_t id,uint32_t generation) {
    assert(c==&client && registered[id-1] && generation==generations[id-1]); return fault();
}
int reist_gui_surface_client_damage(reist_gui_surface_client_t *c,reist_gui_rect_t r) {
    assert(c==&client && r.x>=0 && r.y>=0 && r.width && r.height);
    assert((unsigned)r.x+r.width<=c->width && (unsigned)r.y+r.height<=c->height);
    damage=r; return fault();
}
int reist_gui_surface_client_commit_with_release(reist_gui_surface_client_t *c,uint32_t *id,uint32_t *generation) {
    assert(c==&client); int rc=fault(); if(rc) return rc;
    *id=committed; *generation=committed ? generations[committed-1]+malformed_release : 0;
    committed=committed==1 ? 2 : 1; return 0;
}
static void reset(void) {
    free(arena.surface); arena.surface=NULL; allocations=allocation_fail=0;
    memset(&state,0,sizeof(state)); memset(live,0,sizeof(live)); memset(registered,0,sizeof(registered));
    workspace=&arena; memset(workspace,0,sizeof(*workspace));
    client.width=800;client.height=600; committed=0;
    calls=fail_at=raster_fail=malformed_release=old_width=old_height=0;
    state.loaded=1; scenes[0].version=1;
}
static void paint(unsigned w,unsigned h,unsigned scroll) {
    client.width=w;client.height=h;state.scroll_y=scroll;calls=0;
    assert(!publish_pixels(&state,&client));
    int full=old_width!=w || old_height!=h;
    assert(damage.x==0 && damage.width==w);
    assert(damage.y==(full ? 0 : (int32_t)BROWSER_CONTENT_TOP));
    assert(damage.height==(full ? h : viewport_height(&client)));
    if(!full) for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        if(old_pixels[y*w+x]!=surface_pixels[y*w+x])
            assert(y>=(unsigned)damage.y && y<(unsigned)damage.y+damage.height);
    }
    memcpy(old_pixels,surface_pixels,w*h*4);old_width=w;old_height=h;
    assert(live[0]+live[1]==1 && registered[0]+registered[1]==1);
    assert(state.buffer_id==committed && state.buffer_generation==generations[committed-1]);
}
int main(void) {
    reset();paint(800,600,0);paint(800,600,48);paint(800,600,0);
    paint(900,600,0);paint(900,650,48);paint(800,600,0);
    paint(1600,900,0);paint(1600,900,48);paint(2560,1440,0);paint(2560,1440,48);
    unsigned previous_allocations=allocations;
    paint(800,600,0);paint(2560,1440,0);
    assert(allocations==previous_allocations);
    allocation_fail=1;uint32_t *old_pointer=surface_pixels;
    assert(reserve_surface_pixels(4096,1024)==-12 && surface_pixels==old_pointer);
    allocation_fail=0;paint(4096,1024,0);paint(800,600,0);
    for(unsigned failure=1;failure<=5;++failure) {
        reset();paint(800,600,0);uint32_t old=state.buffer_id, generation=state.buffer_generation;
        calls=0;fail_at=failure;state.scroll_y=48;
        assert(publish_pixels(&state,&client)==-5);
        assert(state.buffer_id==old && state.buffer_generation==generation && committed==old);
        assert(live[0]+live[1]==1 && registered[0]+registered[1]==1);
        fail_at=0;paint(800,600,48);
    }
    reset();paint(800,600,0);raster_fail=1;calls=0;
    assert(publish_pixels(&state,&client)==-27 && !calls && live[0]+live[1]==1);
    reset();paint(800,600,0);malformed_release=1;calls=0;
    assert(publish_pixels(&state,&client)==-84 && live[0]+live[1]==2);
    /* Uncertain release is not authority to destroy the old generation. */
    reset();client.width=0;
    assert(publish_pixels(&state,&client)==-22 && !calls);
    puts("BROWSER_SURFACE_DAMAGE_CLEANUP_OK");return 0;
}
