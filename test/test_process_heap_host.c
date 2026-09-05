#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <reist/libc.h>
extern _Noreturn void _Exit(int status);
#undef assert
#define assert(value) do { if (!(value)) { fprintf(stderr,"assertion line %d: %s\n",__LINE__,#value); _Exit(1); } } while (0)

static jmp_buf fault_target;
static int fault_code;
void reist_libc_fail(unsigned code) { fault_code=(int)code; longjmp(fault_target,1); }
static _Alignas(max_align_t) unsigned char backing[64U*1024U*1024U];
static unsigned char occupied[64];
static unsigned acquired, released, fail_next;
static void *acquire(void *context,size_t size) {
    assert(context==backing && size && size<=sizeof(backing));
    if (fail_next) { fail_next=0; return NULL; }
    unsigned pages=(unsigned)(size+1024U*1024U-1)/(1024U*1024U);
    for (unsigned i=0;i+pages<=64;++i) {
        unsigned j=0; while (j<pages && !occupied[i+j]) ++j;
        if (j!=pages) continue;
        for (j=0;j<pages;++j) occupied[i+j]=1;
        ++acquired; return backing+i*1024U*1024U;
    }
    return NULL;
}
static void release(void *context,void *storage,size_t size) {
    assert(context==backing && (unsigned char *)storage>=backing);
    unsigned at=(unsigned)((unsigned char *)storage-backing)/(1024U*1024U);
    unsigned pages=(unsigned)(size+1024U*1024U-1)/(1024U*1024U);
    assert(at+pages<=64);
    for (unsigned i=0;i<pages;++i) { assert(occupied[at+i]); occupied[at+i]=0; }
    ++released;
}
/* PRODUCTION */
int main(void) {
    reist_libc_backing_t provider={REIST_LIBC_BACKING_VERSION,sizeof(provider),
        32U*1024U*1024U,1024U*1024U,backing,acquire,release};
    reist_libc_stats_t stats={REIST_LIBC_VERSION,sizeof(stats),0,0,0,0};
    provider.version=2; assert(reist_libc_init_backing(&provider)==-EINVAL);
    provider.version=REIST_LIBC_BACKING_VERSION;
    assert(!reist_libc_init_backing(&provider));
    assert(!acquired && !reist_libc_stats(&stats) && !stats.capacity);
    assert(!malloc(SIZE_MAX) && !calloc(SIZE_MAX,2) && !acquired);
    fail_next=1; assert(!malloc(1) && errno==ENOMEM && !acquired);
    unsigned char *first=calloc(1,8U*1024U*1024U);
    assert(first && acquired==1 && !first[0] && !first[8U*1024U*1024U-1]);
    memset(first,0x5a,8U*1024U*1024U);
    unsigned char *second=malloc(8U*1024U*1024U); assert(second && second!=first);
    memset(second,0xa5,8U*1024U*1024U);
    assert(!realloc(first,32U*1024U*1024U) && first[0]==0x5a && second[0]==0xa5);
    assert(reist_libc_reset()==-EBUSY);
    fail_next=1; assert(!realloc(first,12U*1024U*1024U) && first[0]==0x5a);
    unsigned char *grown=realloc(first,12U*1024U*1024U);
    assert(grown && grown[0]==0x5a && grown[8U*1024U*1024U-1]==0x5a);
    assert(released==1 && !reist_libc_stats(&stats) && stats.capacity==20U*1024U*1024U);
    fault_code=0;
    if (!setjmp(fault_target)) { free(grown+1); assert(0); }
    assert(fault_code==REIST_LIBC_FAULT_HEAP && grown[0]==0x5a);
    free(second); free(grown);
    assert(acquired==released && !reist_libc_stats(&stats) && !stats.capacity && !stats.live_objects);
    void *p=malloc(32U*1024U*1024U); assert(p);
    assert(!malloc(1) && errno==ENOMEM); free(p);
    for (unsigned round=0;round<200;++round) {
        void *a=malloc(10), *b=malloc(20); assert(a && b);
        unsigned count=released; free(a); assert(released==count);
        free(b); assert(released==count+1);
    }
    assert(acquired==released && !reist_libc_reset());
    provider.budget=3U*1024U*1024U+4096U;
    assert(!reist_libc_init_backing(&provider));
    p=malloc(3U*1024U*1024U); assert(p);
    void *tail=malloc(1); assert(tail);
    assert(!reist_libc_stats(&stats) && stats.capacity==provider.budget);
    free(p); free(tail); assert(acquired==released && !reist_libc_reset());
    assert(!reist_libc_init(backing,REIST_LIBC_HEAP_LIMIT));
    assert(!malloc(REIST_LIBC_HEAP_LIMIT+1U));
    p=malloc(REIST_LIBC_HEAP_LIMIT); assert(p); free(p);
    assert(!reist_libc_reset());
    puts("PRIVATE_MEMORY_HOST_OK"); return 0;
}
