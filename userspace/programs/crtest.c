/* Real opt-in SDK consumer and bounded process-containment proof. */
#include <x86os.h>
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libwapcaplet/libwapcaplet.h>

static _Alignas(max_align_t) unsigned char arena[256U*1024U];
static void *slots[256];
static void no_strings(lwc_string *s,void *context) { (void)s; (void)context; abort(); }
static int exercise(void) {
    reist_libc_stats_t stats={REIST_LIBC_VERSION,sizeof(stats),0,0,0,0};
    if (reist_libc_init(arena,sizeof(arena)) || reist_libc_stats(&stats) || stats.live_objects)
        return -1;
    unsigned char *p=calloc(17,3);
    if (!p || (uintptr_t)p%_Alignof(max_align_t)) return -1;
    for (unsigned i=0;i<51;++i) if (p[i]) return -1;
    memset(p,0x5a,51);
    if (calloc(SIZE_MAX,2) || realloc(p,SIZE_MAX) || p[0]!=0x5a || errno!=ENOMEM)
        return -1;
    void *grown=realloc(p,129);
    if (!grown || ((unsigned char *)grown)[50]!=0x5a) return -1;
    free(grown);
    unsigned count=0;
    while (count<256 && (slots[count]=malloc(2048))) ++count;
    if (!count || count==256) return -1;
    for (unsigned i=0;i<count;++i) free(slots[i]);
    lwc_string *a=NULL,*b=NULL,*lower=NULL;
    if (lwc_intern_string("REIST",5,&a)!=lwc_error_ok ||
        lwc_intern_string("REIST",5,&b)!=lwc_error_ok || a!=b ||
        lwc_intern_string("reist",5,&lower)!=lwc_error_ok) return -1;
    bool equal=false;
    if (lwc_string_caseless_isequal(a,lower,&equal)!=lwc_error_ok || !equal) return -1;
    lwc_string_unref(a); lwc_string_unref(b); lwc_string_unref(lower);
    lwc_iterate_strings(no_strings,NULL);
    if (reist_libc_stats(&stats) || stats.live_objects || stats.live_bytes ||
        reist_libc_reset()) return -1;
    /* Failure while creating upstream's context must be recoverable. */
    if (reist_libc_init(arena,64) || lwc_intern_string("oom",3,&a)!=lwc_error_oom ||
        reist_libc_stats(&stats) || stats.live_objects || reist_libc_reset()) return -1;
    x86os_puts("REIST_LIBC_ALLOC_UPSTREAM_OK\n"); return 0;
}
static int poll_child(int pid,int *status,uint32_t budget) {
    uint64_t start=0,now=0;
    if (x86os_monotonic_ms(&start)) return -1;
    for (unsigned rounds=0;rounds<12000;++rounds) {
        for (uint32_t i=0;i<32;++i) {
            x86os_process_info_t info;
            int result=x86os_process_info(i,&info);
            if (result<0) return -1;
            if (!result) break;
            if (info.pid==pid) {
                if (info.parent_pid!=x86os_getpid()) return -1;
                if (info.state==X86OS_PROCESS_ZOMBIE)
                    return x86os_wait(pid,status)==pid ? 0 : -1;
            }
        }
        if (x86os_monotonic_ms(&now) || now<start || now-start>=budget) break;
        if (x86os_sleep_ms(1)) return -1;
    }
    return -1;
}
static int child(const char *mode,int expected) {
    const char *argv[]={"/usr/bin/crtest.prg",mode};
    int pid=x86os_spawnv(argv[0],2,argv),status=-1;
    if (pid<=0) return -1;
    if (poll_child(pid,&status,10000)) {
        /* Recheck parent ownership before cleanup; never kill a reused PID. */
        for (uint32_t i=0;i<32;++i) {
            x86os_process_info_t info;
            if (x86os_process_info(i,&info)<=0) break;
            if (info.pid==pid && info.parent_pid==x86os_getpid()) {
                (void)x86os_kill(pid); (void)poll_child(pid,&status,2000); break;
            }
        }
        return -1;
    }
    if (status!=expected || x86os_wait(pid,&status)>=0) return -1;
    x86os_process_identity_t reaped;
    if (x86os_process_identity_of(pid,&reaped)>=0) return -1;
    x86os_puts("REIST_LIBC_CHILD_REAP_OK\n"); return 0;
}
int main(int argc,char **argv) {
    x86os_process_identity_t identity;
    if (x86os_process_identity(&identity)) return 1;
    x86os_puts("CRTEST_GENERATION pid="); x86os_print_number(identity.pid);
    x86os_puts(" generation="); x86os_print_number((int)identity.generation); x86os_puts("\n");
    if (argc==2 && !strcmp(argv[1],"--heap-fault")) {
        if (reist_libc_init(arena,sizeof(arena))) return 1;
        free(arena+1); return 1;
    }
    if (argc==2 && !strcmp(argv[1],"--cpu-fault")) {
        __asm__ volatile("ud2"); return 1;
    }
    if (argc==2 && !strcmp(argv[1],"--abort")) abort();
    if (argc>2 || (argc==2 && strcmp(argv[1],"--child"))) return 2;
    if (exercise()) { x86os_puts("REIST_LIBC_TEST_FAIL\n"); return 1; }
    if (argc==2) return 0;
    if (child("--child",0) || child("--heap-fault",70) ||
        child("--cpu-fault",134) || child("--abort",134) || child("--child",0)) {
        x86os_puts("REIST_LIBC_CHILD_FAIL\n"); return 1;
    }
    x86os_puts("REIST_LIBC_RUNTIME_OK\n"); return 0;
}
