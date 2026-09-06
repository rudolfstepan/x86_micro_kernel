/* Real allocator and upstream dependency; host CRT allocation is not interposed. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <strings.h>
#include <libwapcaplet/libwapcaplet.h>
#include "../userspace/libc/lib/heap.c"

static jmp_buf fault_target;
static int fault_code;
void reist_libc_fail(unsigned code) { fault_code = (int)code; longjmp(fault_target, 1); }
static _Alignas(max_align_t) unsigned char storage[REIST_LIBC_HEAP_LIMIT];
static void *objects[REIST_LIBC_OBJECT_LIMIT];
static void empty_callback(lwc_string *s, void *unused) { (void)s; (void)unused; assert(0); }
static void expect_fault(void *p) {
    fault_code=0;
    if (!setjmp(fault_target)) { free(p); assert(0); }
    assert(fault_code == REIST_LIBC_FAULT_HEAP);
}
static int compare_int(const void *a, const void *b) {
    int x=*(const int *)a, y=*(const int *)b; return (x>y)-(x<y);
}
int main(void) {
    assert(abs(-17)==17 && abs(0)==0 && abs(2147483647)==2147483647);
    assert(tolower('A')=='a' && tolower('z')=='z' && tolower(-1)==-1 && tolower(255)==255);
    assert(!strncasecmp("aBc","AbCd",3) && strncasecmp("a","AB",2)<0);
    assert(strncasecmp("\xff","\x80",1)>0 && !strncasecmp(NULL,NULL,0));
    char copied[5]={'x','x','x','x','x'};
    assert(strncpy(copied,"a",4)==copied && copied[0]=='a' && !copied[1] && !copied[3] && copied[4]=='x');
    strncpy(copied,"abcd",2); assert(copied[0]=='a' && copied[1]=='b' && !copied[2]);
    int sorted[]={1,3,5,7,9}, key=5;
    assert(bsearch(&key,sorted,5,sizeof(int),compare_int)==sorted+2);
    key=2; assert(!bsearch(&key,sorted,5,sizeof(int),compare_int));
    assert(!bsearch(NULL,NULL,0,sizeof(int),compare_int));
    assert(!bsearch(&key,sorted,SIZE_MAX,sizeof(int),compare_int));
    assert(!malloc(1) && errno==ENOMEM);
    assert(!strdup("unbound") && errno==ENOMEM);
    assert(reist_libc_init(storage+1,sizeof(storage)-1)==-EINVAL);
    assert(reist_libc_init(storage,sizeof(storage)+1)==-EINVAL);
    assert(!reist_libc_init(storage,sizeof(storage)));
    char *duplicate=strdup("CSS"); assert(duplicate && !strcmp(duplicate,"CSS"));
    duplicate[0]='x'; assert(!strcmp(duplicate,"xSS")); free(duplicate);
    duplicate=strdup(""); assert(duplicate && !duplicate[0]); free(duplicate);
    assert(!malloc(0) && !calloc(0,9)); free(NULL);
    assert(!calloc(SIZE_MAX,2) && errno==ENOMEM);
    assert(!malloc(SIZE_MAX) && errno==ENOMEM);
    unsigned char *p=calloc(17,3);
    assert(p && (uintptr_t)p % _Alignof(max_align_t)==0);
    for (unsigned i=0;i<51;++i) assert(!p[i]);
    memset(p,0x5a,51);
    assert(reist_libc_reset()==-EBUSY);
    assert(reist_libc_init(storage,sizeof(storage))==-EBUSY);
    assert(!realloc(p,SIZE_MAX) && p[0]==0x5a && p[50]==0x5a);
    p=realloc(p,1000); assert(p);
    for (unsigned i=0;i<51;++i) assert(p[i]==0x5a);
    assert(realloc(p,10)==p);
    expect_fault(p+1); expect_fault((void *)(uintptr_t)1);
    assert(p[0]==0x5a); free(p); expect_fault(p);
    p=malloc(64); assert(p); assert(!realloc(p,0));
    for (unsigned i=0;i<REIST_LIBC_OBJECT_LIMIT;++i) {
        objects[i]=malloc(17); assert(objects[i]);
        memset(objects[i],(int)(i&255),17);
    }
    assert(!malloc(1) && errno==ENOMEM);
    for (unsigned i=0;i<REIST_LIBC_OBJECT_LIMIT;i+=2) free(objects[i]);
    for (unsigned i=1;i<REIST_LIBC_OBJECT_LIMIT;i+=2) {
        unsigned char *b=objects[i];
        for (unsigned j=0;j<17;++j) assert(b[j]==(i&255));
        free(b);
    }
    p=malloc(sizeof(storage)); assert(p); free(p);
    reist_libc_stats_t stats={REIST_LIBC_VERSION,sizeof(stats),0,0,0,0};
    assert(!reist_libc_stats(&stats) && !stats.live_objects && !stats.live_bytes);
    /* Corrupt private metadata: stop before a user pointer or any state is used. */
    heap_blocks[0].check ^= 1;
    fault_code=0;
    if (!setjmp(fault_target)) { (void)malloc(4); assert(0); }
    assert(fault_code==REIST_LIBC_FAULT_HEAP);
    heap_blocks[0].check ^= 1;
    assert(!reist_libc_reset());
    /* Upstream init OOM and retry, then all remaining ordinary allocation failures. */
    assert(!reist_libc_init(storage,64));
    lwc_string *a=NULL, *b=NULL, *c=NULL;
    assert(lwc_intern_string("Test",4,&a)==lwc_error_oom);
    assert(!reist_libc_stats(&stats) && !stats.live_objects);
    assert(!reist_libc_reset());
    assert(!reist_libc_init(storage,sizeof(storage)));
    assert(lwc_intern_string("Test",4,&a)==lwc_error_ok);
    assert(lwc_intern_string("Test",4,&b)==lwc_error_ok && a==b);
    assert(lwc_intern_string("test",4,&c)==lwc_error_ok);
    bool equal=false;
    assert(lwc_string_caseless_isequal(a,c,&equal)==lwc_error_ok && equal);
    lwc_string_unref(b); lwc_string_unref(a); lwc_string_unref(c);
    lwc_iterate_strings(empty_callback,NULL);
    assert(!reist_libc_stats(&stats) && !stats.live_objects);
    assert(lwc_intern_string("kept",4,&a)==lwc_error_ok);
    unsigned count=0;
    while (count<REIST_LIBC_OBJECT_LIMIT && (objects[count]=malloc(1))) ++count;
    assert(count>0 && count<REIST_LIBC_OBJECT_LIMIT);
    assert(lwc_intern_string("new",3,&b)==lwc_error_oom);
    assert(!memcmp(lwc_string_data(a),"kept",4));
    for (unsigned i=0;i<count;++i) free(objects[i]);
    assert(lwc_intern_string("new",3,&b)==lwc_error_ok);
    lwc_string_unref(a); lwc_string_unref(b); lwc_iterate_strings(empty_callback,NULL);
    assert(!reist_libc_stats(&stats) && !stats.live_objects);
    char text[16]="abcdef";
    memmove(text+1,text,6); assert(!memcmp(text,"aabcdef",7));
    memmove(text,text+1,6); assert(!memcmp(text,"abcdef",6));
    assert(memchr(text,'c',6)==text+2 && !memchr(text,'z',6));
    assert(strcmp("\xff","\x01")>0 && strncmp("ab","ac",1)==0);
    assert(strchr("abc",0)!=NULL && strrchr("aba",'a')[1]==0);
    assert(strlen("abc")==3);
    assert(!reist_libc_reset());
    puts("REIST_LIBC_HOST_OK"); return 0;
}
