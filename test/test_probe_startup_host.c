#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "include/kernel/supervisor.h"

enum { PROCESS_DOMAIN_PROBE=1 };
typedef uint32_t x86os_ipc_handle_t;
#define X86OS_REIST_REPORT_SELF_TEST REIST_REPORT_SELF_TEST
#define X86OS_REIST_REPORT_PROGRESS REIST_REPORT_PROGRESS
/* Only the private delivery fields accessed by the real spawn are needed. */
static struct {
    supervisor_protected_probe_control_t control;
    uint32_t frame_delivery_pid,frame_delivery_generation,frame_delivery_ethertype,frame_delivery_pending;
    uint32_t ipv4_delivery_pid,ipv4_delivery_generation,ipv4_delivery_pending;
    uint32_t icmp_delivery_pid,icmp_delivery_generation,icmp_delivery_crc32,icmp_delivery_pending;
    uint32_t udp_delivery_pid,udp_delivery_generation,udp_delivery_pending;
    uint32_t dhcp_delivery_pid,dhcp_delivery_generation,dhcp_delivery_crc32,dhcp_delivery_pending;
} probe_runtime;
static supervisor_probe_control_t saved;
static volatile uint32_t network_service_ap_execution_generation;
static unsigned prepared,alive,spawns,starts,kills,writes,reads,self_tests,progresses,fences;
static unsigned fail_read,fail_create,fail_identity,fail_write,fail_start,fail_restore,fail_kill,fail_delivery;
static unsigned immediate,exit_immediately;
static int current_pid=71;
static uint32_t current_generation=9;
static int startup_result;
static char mode_seen[16];
static bool probe_control_valid(const void *,size_t);
static int startup_report(int,uint32_t,uint32_t,uint32_t,uint64_t);
static int report_startup(x86os_ipc_handle_t *);

int supervisor_protected_probe_control_read(supervisor_protected_probe_control_t *object,supervisor_probe_control_t *out) {
    assert(object==&probe_runtime.control); ++reads;
    if(fail_read || !probe_control_valid(&saved,sizeof(saved))) return -1;
    *out=saved; return 0;
}
int supervisor_protected_probe_control_write(supervisor_protected_probe_control_t *object,const supervisor_probe_control_t *in) {
    assert(object==&probe_runtime.control); ++writes;
    if(writes==fail_write || (fail_restore && writes==2)) return -1;
    assert(probe_control_valid(in,sizeof(*in)));
    saved=*in; return 0;
}
static void netdev_reset_service_frames(void) { assert(!alive); }
static int icmp_delivery_clear(void) { return fail_delivery==1 ? -1 : 0; }
static int udp_delivery_clear(void) { return fail_delivery==2 ? -1 : 0; }
static bool process_identity_alive(int pid,uint32_t generation) {
    return alive && pid==current_pid && generation==current_generation;
}
static int process_get_identity(int pid,uint32_t *generation) {
    if(fail_identity || !alive || pid!=current_pid) return -1;
    *generation=current_generation; return 0;
}
static int process_terminate(int pid) {
    assert(pid==current_pid); ++kills;
    if(fail_kill) return -1;
    assert(prepared); /* Rollback must never race a running child. */
    alive=prepared=0; return 0;
}
static void output_fence_all(void) { ++fences; }
static void scheduler_preempt_disable(void) {}
static void scheduler_preempt_enable(void) {}
static void run_child(void) {
    assert(alive && !prepared);
    x86os_ipc_handle_t endpoint=0;
    startup_result=report_startup(&endpoint);
    if(startup_result || exit_immediately) alive=0;
}
static int admit(const char *path,int argc,const char *const *argv,uint32_t domain,unsigned hold) {
    assert(!alive && argc==2 && domain==PROCESS_DOMAIN_PROBE);
    assert(!strcmp(path,"/libexec/reist/reist.prg") && !strcmp(argv[0],"reist.prg"));
    assert(strlen(argv[1])<sizeof(mode_seen)); strcpy(mode_seen,argv[1]);
    ++spawns;
    if(fail_create) return -1;
    prepared=hold; alive=1;
    if(immediate && !hold) run_child();
    return current_pid;
}
int supervisor_spawn_service(const char *path,int argc,const char *const *argv,uint32_t domain) {
    return admit(path,argc,argv,domain,0);
}
static int process_spawn_supervised_prepared(const char *path,int argc,const char *const *argv,uint32_t domain) {
    return admit(path,argc,argv,domain,1);
}
static int process_start_prepared_supervised(int pid,uint32_t generation) {
    ++starts; assert(prepared && process_identity_alive(pid,generation));
    assert(saved.pid==pid && saved.process_generation==generation && !saved.healthy && !saved.service_ready);
    if(fail_start) return -1;
    prepared=0;
    if(immediate) run_child();
    return 0;
}
static int compositor_report_if_identity(int p,uint32_t g,uint32_t t,uint32_t v,uint64_t n,bool *match) {
    (void)p;(void)g;(void)t;(void)v;(void)n; *match=false; return 0;
}
#define audio_service_report_if_identity compositor_report_if_identity
static int supervisor_force_isolate(supervisor_handle_t handle) { (void)handle; ++fences; return 0; }
int supervisor_report_self_test(supervisor_handle_t handle,bool ok,uint64_t now) {
    (void)handle;(void)now; assert(ok && alive && !prepared); ++self_tests; return 0;
}
int supervisor_report_progress(supervisor_handle_t handle,uint64_t value,uint64_t now) {
    (void)handle;(void)now; assert(value==1 && self_tests>progresses); ++progresses; return 0;
}
static void probe_report_recovery_pair(uint32_t count) { assert(count>=1); }
static uint32_t x86_cpu_current_index(void) { return 0; }
static int process_set_supervised_affinity(int pid,uint32_t generation,uint32_t mask) {
    assert(process_identity_alive(pid,generation) && mask); return 0;
}
static int x86os_ipc_create(x86os_ipc_handle_t *out) { *out=1000U+current_generation; return 0; }
static int x86os_reist_report(uint32_t kind,uint32_t value) {
    return startup_report(current_pid,current_generation,kind,value,100);
}

/* PRODUCTION */

static void reset(void) {
    memset(&probe_runtime,0,sizeof(probe_runtime));
    saved=(supervisor_probe_control_t){.active=1,.handle={0,1,1}};
    prepared=alive=spawns=starts=kills=writes=reads=self_tests=progresses=fences=0;
    fail_read=fail_create=fail_identity=fail_write=fail_start=fail_restore=fail_kill=fail_delivery=0;
    immediate=1; exit_immediately=0; startup_result=-99;
}
int main(void) {
    reset();
    /* Adversarial scheduling executes the real Ring-3 startup before spawn
     * returns. Old code rejects SELF_TEST and loses the child identity. */
    assert(probe_spawn_next());
    assert(startup_result==0 && self_tests==1 && progresses==1 && alive);
    assert(saved.healthy && saved.endpoint_handle==1000U+current_generation);
    assert(starts==1 && !prepared && saved.launch_count==1 && !strcmp(mode_seen,"crash"));
    assert(startup_report(current_pid,current_generation-1,REIST_REPORT_SELF_TEST,77,100)==-1);
    assert(startup_report(current_pid+1,current_generation,REIST_REPORT_SELF_TEST,77,100)==-1);
    /* A child may exit after successful admission. No post-start lookup may
     * convert that into a spawn failure or overwrite its report updates. */
    reset(); exit_immediately=1;
    assert(probe_spawn_next() && !alive && startup_result==0 && saved.healthy);
    reset(); immediate=0;
    assert(probe_spawn_next() && alive && !prepared && !saved.healthy && saved.fenced);
    run_child(); assert(startup_result==0 && saved.healthy && !saved.fenced);
    for(unsigned mode=0;mode<4;++mode) {
        reset();
        if(mode) saved=(supervisor_probe_control_t){.active=1,.fenced=1,.handle={0,1,1},
            .pid=12,.process_generation=2,.launch_count=mode,.endpoint_handle=22};
        assert(probe_spawn_next());
        const char *expected[]={"crash","hang","invalid","healthy"};
        assert(!strcmp(mode_seen,expected[mode]) && saved.launch_count==mode+1 && saved.healthy && !saved.fenced);
    }
    for(unsigned stage=0;stage<7;++stage) {
        reset();
        if(stage==0) fail_read=1;
        if(stage==1) fail_delivery=1;
        if(stage==2) fail_delivery=2;
        if(stage==3) fail_create=1;
        if(stage==4) fail_identity=1;
        if(stage==5) fail_write=1;
        if(stage==6) fail_start=1;
        assert(!probe_spawn_next());
        assert(!alive && !prepared && !self_tests && !progresses);
        assert(kills==(stage>=4 ? 1U : 0U));
        assert(!saved.healthy && !saved.service_ready && saved.pid==0 && saved.launch_count==0);
    }
    reset(); saved=(supervisor_probe_control_t){.active=1,.fenced=1,.handle={0,1,1},
        .pid=12,.process_generation=2,.launch_count=3,.endpoint_handle=22};
    supervisor_probe_control_t before=saved;
    fail_start=1;
    assert(!probe_spawn_next() && !alive && !prepared && kills==1);
    assert(!memcmp(&saved,&before,sizeof(saved)));
    reset(); fail_start=fail_restore=1;
    assert(!probe_spawn_next() && !alive && kills==1 && fences);
    reset(); fail_identity=fail_kill=1;
    assert(!probe_spawn_next() && prepared && !self_tests && fences);
    for(unsigned denied=0;denied<4;++denied) {
        reset();
        if(denied==0) saved=(supervisor_probe_control_t){0};
        else saved=(supervisor_probe_control_t){.active=1,.fenced=denied==1 ? 0 : 1,.handle={0,1,1},
            .pid=current_pid,.process_generation=current_generation,.launch_count=denied==3 ? UINT32_MAX : 1};
        if(denied==2) alive=1;
        assert(!probe_spawn_next() && !spawns && !starts && !kills);
    }
    puts("PROBE_STARTUP_HOST_OK immediate-report early-exit four-generations stale-identity rollback-integrity-cleanup");
    return 0;
}
