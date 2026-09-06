#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define PROCESS_PATH_MAX 256
#define PAGE_SIZE 4096U
#define USER_STACK_TOP 0x10000000U
#define USER_STACK_BOTTOM (USER_STACK_TOP-32768U)
typedef int page_directory_t;
typedef int Process;
static Process parent;
static unsigned allocations, frees, fail_alloc, copies, fail_copy, spawns, writes, fail_write;
static unsigned char stack_bytes[32768];
static char text[8194];
static void *k_malloc(size_t n) { assert(n<=16384); ++allocations; return fail_alloc ? NULL : malloc(n); }
static void k_free(void *p) { assert(p); ++frees; free(p); }
static Process *scheduler_current_process(void) { return &parent; }
static int copy_from_user(void *out,const void *in,size_t n) {
    ++copies;
    if((uintptr_t)in<4096 || (fail_copy && copies==fail_copy)) return -1;
    memcpy(out,in,n); return 0;
}
static int copy_string_from_user(char *out,size_t n,const char *in) {
    for(size_t i=0;i<n;++i) { if(copy_from_user(out+i,in+i,1)) return -1; if(!out[i]) return (int)i; }
    return -1;
}
static uint32_t pit_monotonic_ms(void) { return 0; }
static int supervisor_start_compositor(uint32_t now,uint32_t flags,int *pid) { (void)now;(void)flags; *pid=43; return 1; }
static int process_spawn_args(Process *p,const char *path,int argc,const char *const *argv) {
    assert(p==&parent && !strcmp(path,"/curl.prg") && argc>=2);
    assert(argv[1]!=text && !strcmp(argv[1],text)); ++spawns; return 42;
}
static int copy_to_user_space(page_directory_t *pd,uint32_t address,const void *data,size_t n) {
    assert(pd && address>=USER_STACK_BOTTOM && address<=USER_STACK_TOP && n<=USER_STACK_TOP-address);
    ++writes; if(fail_write && writes==fail_write) return -1;
    memcpy(stack_bytes+address-USER_STACK_BOTTOM,data,n); return 0;
}
/* PRODUCTION */
int main(void) {
    const char *argv[]={"/curl.prg",text,text};
    memset(text,'x',8192); text[8192]=0;
    assert(syscall_spawnv(argv[0],argv,2)==42);
    assert(spawns==1 && allocations==frees && copies<2000);
    unsigned accepted=spawns;
    assert(syscall_spawnv(argv[0],argv,3)==-7 && spawns==accepted && allocations==frees);
    text[8192]='x'; text[8193]=0;
    assert(syscall_spawnv(argv[0],argv,2)==-7 && spawns==accepted && allocations==frees);
    text[8192]=0;
    assert(syscall_spawnv(argv[0],(const char *const *)1,2)==-14 && allocations==frees);
    assert(syscall_spawnv(argv[0],(const char *const *)(UINTPTR_MAX-2U),2)==-14 && allocations==frees);
    copies=0; fail_copy=10;
    assert(syscall_spawnv(argv[0],argv,2)==-14 && allocations==frees); fail_copy=0;
    fail_alloc=1; assert(syscall_spawnv(argv[0],argv,2)==-12); fail_alloc=0;
    --allocations; assert(allocations==frees && spawns==accepted);
    assert(syscall_spawnv(argv[0],argv,17)==-22);
    page_directory_t pd=1; uint32_t sp=0x12345678;
    assert(!build_user_arguments(&pd,2,argv,&sp));
    assert(sp>=USER_STACK_BOTTOM+16384 && !(sp&3));
    uint32_t values[3]; memcpy(values,stack_bytes+sp-USER_STACK_BOTTOM,sizeof(values));
    assert(!values[0] && values[1]==2);
    uint32_t pointers[3]; memcpy(pointers,stack_bytes+values[2]-USER_STACK_BOTTOM,sizeof(pointers));
    assert(!pointers[2] && !strcmp((char *)stack_bytes+pointers[1]-USER_STACK_BOTTOM,text));
    writes=0; sp=0x12345678;
    assert(build_user_arguments(&pd,3,argv,&sp)<0 && !writes && sp==0x12345678);
    text[8192]='x'; assert(build_user_arguments(&pd,2,argv,&sp)<0 && !writes); text[8192]=0;
    fail_write=2;
    assert(build_user_arguments(&pd,2,argv,&sp)<0 && sp==0x12345678 && writes==2);
    puts("PROCESS_ARGUMENTS_OK"); return 0;
}
