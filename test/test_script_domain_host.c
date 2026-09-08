#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/reist/abi/syscall.h"
#undef SYS_OPEN /* Windows CRT internal constant, not the REIST syscall. */
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define NUMBER(kernel,sdk,n) SYS_##kernel=n,
enum { REIST_SYSCALL_LIST(NUMBER) };
/* DEFINITIONS */
typedef struct Process {
    int pid; uint32_t generation; int parent_pid; uint32_t parent_generation;
    int task_id,exit_status; char name[32];
    bool is_running,has_exited,terminating;
    uint32_t heap_bytes,heap_budget;
    process_domain_profile_t domain_profile;
} Process;
typedef struct { int32_t pid,parent_pid,state,exit_status; char name[32]; } process_info_t;
enum { PROCESS_STATE_READY,PROCESS_STATE_RUNNING,PROCESS_STATE_SLEEPING,
       PROCESS_STATE_WAITING,PROCESS_STATE_ZOMBIE };
enum { TASK_RUNNING,TASK_SLEEPING,TASK_WAITING };
typedef struct { uint32_t eax,ebx,ecx,edx,cs; } Registers;
static Process process_list[MAX_PROGRAMS],*current;
static unsigned locks,reads,writes,lookups,dispatches,timings;
static int copy_failure,identity_failure,state_failure;
static uint32_t process_table_lock_irqsave(void) { CHECK(!locks); ++locks; return 7; }
static void process_table_unlock_irqrestore(uint32_t flags) { CHECK(locks==1 && flags==7); --locks; }
static Process *scheduler_current_process(void) { return current; }
static int scheduler_task_state_snapshot(int id,Process *p,uint32_t gen,int *state) {
    CHECK(locks==1 && id==p->task_id && gen==p->generation);
    *state=TASK_RUNNING; return state_failure;
}
static int copy_from_user(void *dst,const void *src,size_t size) {
    ++reads; if(copy_failure || (uintptr_t)src<4096) return -1;
    memcpy(dst,src,size); return 0;
}
static int copy_to_user(void *dst,const void *src,size_t size) {
    ++writes; if(copy_failure || (uintptr_t)dst<4096) return -1;
    memcpy(dst,src,size); return 0;
}
static int process_get_identity(int pid,uint32_t *gen) {
    ++lookups; if(identity_failure) return -1;
    for(unsigned i=0;i<MAX_PROGRAMS;++i)
        if(process_list[i].pid==pid && process_list[i].is_running) {
            *gen=process_list[i].generation; return 0;
        }
    return -1;
}
static uint64_t runtime_timing_begin(void) { ++timings; return 1; }
/* PRODUCTION */
static void init(Process *p,process_domain_kind_t kind) {
    memset(p,0,sizeof(*p)); p->pid=9; p->generation=7; p->is_running=true;
    CHECK(initialize_domain_profile(&p->domain_profile,kind));
}
static void unchanged(Process before,int expected) {
    CHECK(process_restrict_script(current)==expected);
    CHECK(!memcmp(&before,current,sizeof(before)) && !locks);
}
static bool permitted(uint32_t call) {
    static const uint8_t allow[]={4,5,6,9,22,26,40,41,42,53,54,58,114,128};
    for(unsigned i=0;i<sizeof(allow);++i) if(call==allow[i]) return true;
    return false;
}
static void profiles(void) {
    Process old;
    for(unsigned kind=1;kind<=9;++kind) {
        init(current,(process_domain_kind_t)kind); old=*current;
        CHECK(baseline_profile(&old.domain_profile,(process_domain_kind_t)kind));
        for(unsigned call=0;call<128;++call)
            CHECK(process_syscall_allowed(current,call)==process_syscall_allowed(&old,call));
        CHECK(process_syscall_allowed(current,128)==(kind==1));
        if(kind!=1) unchanged(*current,-13);
    }
    init(current,PROCESS_DOMAIN_COMPATIBILITY);
    current->heap_budget=PROCESS_HEAP_MAX_BYTES;
    current->heap_bytes=PROCESS_SCRIPT_HEAP_MAX_BYTES+4096; unchanged(*current,-12);
    current->heap_bytes=4096;
    CHECK(!process_restrict_script(current));
    CHECK(current->heap_budget==PROCESS_SCRIPT_HEAP_MAX_BYTES && current->heap_bytes==4096);
    unchanged(*current,0);
    for(unsigned call=0;call<513;++call) {
        CHECK(process_syscall_allowed(current,call)==permitted(call));
        Registers r={call,4,4,4,3}; unsigned d=dispatches,t=timings;
        syscall_handler(&r);
        CHECK(dispatches==d+(permitted(call)?1:0));
        CHECK(timings==t); // timing syscall must fail before timing_begin
        if(!permitted(call)) CHECK(r.eax==(uint32_t)-13);
    }
    CHECK(!process_syscall_allowed(current,UINT32_MAX));
    current->heap_budget=4096; unchanged(*current,0);
    init(current,PROCESS_DOMAIN_COMPATIBILITY); current->heap_budget=0;
    CHECK(!process_restrict_script(current) && !current->heap_budget);
    for(unsigned failure=0;failure<7;++failure) {
        init(current,PROCESS_DOMAIN_COMPATIBILITY);
        if(failure==0) ++current->domain_profile.version;
        if(failure==1) --current->domain_profile.struct_size;
        if(failure==2) current->domain_profile.kind=99;
        if(failure==3) current->generation=0;
        if(failure==4) current->terminating=true;
        if(failure==5) current->is_running=false;
        if(failure==6) current->domain_profile.allowed_syscalls[0]&=~(1U<<SYS_GETPID);
        unchanged(*current,-13);
    }
    Process *saved=current; current=NULL;
    Registers r={SYS_RUNTIME_TIMING,0,0,0,3}; unsigned t=timings;
    syscall_handler(&r); CHECK(r.eax==(uint32_t)-13 && t==timings);
    CHECK(process_restrict_script(NULL)==-13); current=saved;
}
static void requests(void) {
    init(current,PROCESS_DOMAIN_COMPATIBILITY);
    const reist_process_restrict_request_t valid={1,16,1,0};
    CHECK(sizeof(valid)==16);
    for(unsigned i=0;i<5;++i) {
        reist_process_restrict_request_t bad=valid;
        if(i==0) ++bad.version;
        if(i==1) ++bad.struct_size;
        if(i==2) bad.profile=0;
        if(i==3) bad.profile=UINT32_MAX;
        if(i==4) bad.reserved=1;
        Process before=*current;
        CHECK(syscall_process_restrict(&bad)==-22);
        CHECK(!memcmp(current,&before,sizeof(before)));
    }
    Process before=*current;
    CHECK(syscall_process_restrict(NULL)==-14);
    CHECK(syscall_process_restrict((void *)4)==-14);
    copy_failure=1; CHECK(syscall_process_restrict(&valid)==-14); copy_failure=0;
    CHECK(!memcmp(current,&before,sizeof(before)));
    CHECK(!syscall_process_restrict(&valid));
    before=*current; CHECK(!syscall_process_restrict(&valid));
    CHECK(!memcmp(current,&before,sizeof(before)));
}
static void visibility(void) {
    for(unsigned i=0;i<3;++i) {
        init(&process_list[i],PROCESS_DOMAIN_COMPATIBILITY);
        process_list[i].pid=8+(int)i; process_list[i].generation=6+i;
    }
    current=&process_list[1]; current->parent_pid=8; current->parent_generation=6;
    CHECK(!process_restrict_script(current));
    process_info_t info;
    CHECK(syscall_process_info(0,&info)==1 && info.pid==9 && info.parent_pid==8);
    process_info_t before=info;
    CHECK(syscall_process_info(1,&info)==0 && !memcmp(&info,&before,sizeof(info)));
    CHECK(syscall_process_info(UINT32_MAX,&info)==0);
    CHECK(syscall_process_info(0,(void *)4)==-14);
    CHECK(process_get_info(0,&info)==1 && info.pid==8); // trusted kernel view
    state_failure=1;
    CHECK(syscall_process_info(0,&info)==1 && info.state==PROCESS_STATE_READY);
    state_failure=0;
    uint32_t identity[4]={0};
    CHECK(!syscall_process_identity_of(9,identity) && identity[3]==7);
    CHECK(!syscall_process_identity_of(8,identity) && identity[3]==6);
    unsigned l=lookups,w=writes;
    CHECK(syscall_process_identity_of(10,identity)==-13 && lookups==l && writes==w);
    CHECK(syscall_process_identity_of(2147483647,identity)==-13 && lookups==l);
    ++process_list[0].generation;
    CHECK(syscall_process_identity_of(8,identity)==-3 && writes==w);
    identity_failure=1; CHECK(syscall_process_identity_of(9,identity)==-3); identity_failure=0;
    CHECK(syscall_process_identity_of(9,(void *)4)==-14);
    current=&process_list[2];
    CHECK(!syscall_process_identity_of(8,identity)); // old callers unchanged
    CHECK(syscall_process_info(0,&info)==1 && info.pid==8);
    CHECK(!locks);
}
int main(void) {
    current=&process_list[1]; profiles(); requests(); visibility();
    puts("SCRIPT_DOMAIN_HOST_OK masks=129 legacy=9 native_gate=513 visibility=bounded attenuation=irreversible");
    return 0;
}
