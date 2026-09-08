#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "drivers/video/framebuffer.h"
#include "mm/kmalloc.h"
static unsigned locked,preempt,allocations,fail_alloc,low_memory,revoke_on_draw;
typedef int spinlock_t;
#define SPINLOCK_INIT 0
static uint32_t spinlock_acquire_irq(spinlock_t *p) { (void)p;assert(!locked);locked=1;return 0; }
static void spinlock_release_irq(spinlock_t *p,uint32_t f) { (void)p;(void)f;assert(locked);locked=0; }
static void scheduler_preempt_disable(void) { ++preempt; }
static void scheduler_preempt_enable(void) { assert(preempt);--preempt; }
static uint32_t host_storage[64U*1024U*1024U/4U],input[16U*1024U*1024U/4U];
static void memory_get_stats_host(memory_stats_t *s) {
    memset(s,0,sizeof(*s));s->managed_bytes=1024ULL*1024*1024;
    s->free_frame_bytes=low_memory ? 32ULL*1024*1024 : 512ULL*1024*1024;
}
#define memory_get_stats memory_get_stats_host
static void *k_malloc_host(size_t n) {
    assert(n==sizeof(host_storage) && preempt && !locked);++allocations;
    return fail_alloc ? NULL : host_storage;
}
#define k_malloc k_malloc_host
static uint8_t *fb_address=(uint8_t *)host_storage;
static uint32_t fb_width=4096,fb_height=4096;
static void framebuffer_surface_buffer_process_cleanup(int,uint32_t);
int framebuffer_frame_draw_enter(int p,uint32_t g,uint64_t t) { (void)p;(void)g;(void)t;return 0; }
int framebuffer_frame_draw_leave(int p,uint32_t g,uint64_t t) { (void)p;(void)g;(void)t;return 0; }
bool framebuffer_write_xrgb8888_span(uint32_t x,uint32_t y,const uint32_t *p,uint32_t n) {
    (void)x;(void)y;assert(n && p[0]==0x112233);
    if(revoke_on_draw) { revoke_on_draw=0;framebuffer_surface_buffer_process_cleanup(1,1); }
    return true;
}
bool framebuffer_present_pixels(uint32_t x,uint32_t y,uint32_t w,uint32_t h) { (void)x;(void)y;(void)w;(void)h;return true; }
@STORAGE@
@OPERATIONS@
int main(void) {
    unsigned id=77,g=88;
    low_memory=1;
    assert(framebuffer_surface_buffer_create(1,1,2,1,1600,900,1600,input,1600*900,&id,&g)==-12);
    assert(!allocations && !surface_buffer_storage && id==77 && g==88 && !preempt);
    low_memory=0;fail_alloc=1;
    assert(framebuffer_surface_buffer_create(1,1,2,1,1600,900,1600,input,1600*900,&id,&g)==-12);
    assert(allocations==1 && !surface_buffer_storage && !surface_buffer_storage_pending && !preempt);
    fail_alloc=0; input[0]=0x112233;
    assert(!framebuffer_surface_buffer_create(1,1,2,1,2560,1440,2560,input,2560*1440,&id,&g));
    assert(allocations==2 && surface_buffers[id-1].pixels[0]==0x112233);
    assert(framebuffer_surface_buffer_draw(3,1,1,1,id,g,0,0,0,0,1,1,0)==-13);
    assert(framebuffer_surface_buffer_destroy(1,2,id,g)==-116);
    revoke_on_draw=1;
    assert(!framebuffer_surface_buffer_draw(2,1,1,1,id,g,0,0,0,0,1,1,0));
    assert(!surface_buffers[id-1].pixels && !surface_buffers[id-1].references);
    assert(framebuffer_surface_buffer_destroy(1,1,id,g)==-116);
    unsigned ids[4],gens[4];
    for(unsigned i=0;i<4;++i)
        assert(!framebuffer_surface_buffer_create(1,1,2,1,4096,1024,4096,input,4096*1024,&ids[i],&gens[i]));
    assert(framebuffer_surface_buffer_create(1,1,2,1,1,1,1,input,1,&id,&g)==-75);
    framebuffer_surface_buffer_process_cleanup(2,1);
    framebuffer_surface_buffer_process_cleanup(2,1);
    for(unsigned i=0;i<FB_SURFACE_BUFFER_BITMAP_WORDS;++i) assert(!surface_buffer_block_bitmap[i]);
    input[0]=0xff000000;
    assert(framebuffer_surface_buffer_create(1,1,2,1,1600,900,1600,input,1600*900,&id,&g)==-22);
    for(unsigned i=0;i<FB_SURFACE_BUFFER_BITMAP_WORDS;++i) assert(!surface_buffer_block_bitmap[i]);
    input[0]=0x112233;
    assert(!framebuffer_surface_buffer_create(1,1,2,1,1600,900,1600,input,1600*900,&id,&g));
    assert(id==ids[0] && g!=gens[0]);
    assert(!framebuffer_surface_buffer_destroy(1,1,id,g));
    unsigned before=allocations;
    assert(framebuffer_surface_buffer_create(1,1,2,1,4096,4096,4096,input,4096*4096,&id,&g)==-22);
    assert(framebuffer_surface_buffer_create(1,1,2,1,4097,1,4097,input,4097,&id,&g)==-22);
    assert(framebuffer_surface_buffer_create(1,1,2,1,1,2,UINT32_MAX,input,2,&id,&g)==-22);
    assert(framebuffer_surface_buffer_create(1,0,2,1,1,1,1,input,1,&id,&g)==-22);
    assert(allocations==before && !locked && !preempt);
    return 0;
}
