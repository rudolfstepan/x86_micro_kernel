/* Shell-reachable private-memory admission, collection and containment proof. */
#include <x86os.h>
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MIB (1024U*1024U)
static int children[2];
static uint32_t generations[2];
typedef struct { uint32_t type, a, b; } memory_message_t;
static const char *phase="admission";
static int failed_call(const char *stage,int result) {
    x86os_puts("PRIVATE_MEMORY_CALL "); x86os_puts(stage);
    x86os_puts(" result="); x86os_print_number(result); x86os_puts("\n"); return -1;
}
static uint32_t decimal(const char *s) {
    uint32_t value=0;
    for (unsigned i=0;i<10;++i) {
        if (!s[i]) return value;
        if (s[i]<'0' || s[i]>'9' || value>(UINT32_MAX-(unsigned)(s[i]-'0'))/10U) return 0;
        value=value*10U+(unsigned)(s[i]-'0');
    }
    return s[10] ? 0 : value;
}
static void number(char out[11],uint32_t value) {
    char reversed[10]; unsigned n=0;
    do { reversed[n++]=(char)('0'+value%10U); value/=10U; } while (value && n<10);
    for (unsigned i=0;i<n;++i) out[i]=reversed[n-i-1];
    out[n]=0;
}
static int send_value(x86os_ipc_handle_t endpoint,uint32_t kind,uint32_t a,uint32_t b) {
    x86os_ipc_message_t message={X86OS_IPC_MESSAGE_VERSION,sizeof(message),12,{0}};
    memory_message_t data={kind,a,b};
    memcpy(message.payload,&data,sizeof(data));
    return x86os_ipc_send_timeout(endpoint,&message,1000);
}
static int try_receive(x86os_ipc_handle_t endpoint,memory_message_t *message) {
    x86os_ipc_message_t wire={X86OS_IPC_MESSAGE_VERSION,sizeof(wire),0,{0}};
    /* The legacy receive wrapper waits up to one second. A heartbeat must
     * poll explicitly, otherwise it measures its own IPC wait as starvation. */
    int result=x86os_ipc_receive_timeout(endpoint,&wire,0);
    if (result) return result;
    if (wire.version!=X86OS_IPC_MESSAGE_VERSION || wire.struct_size!=sizeof(wire) ||
        wire.length!=sizeof(*message)) return -22;
    memcpy(message,wire.payload,sizeof(*message)); return 0;
}
static int receive(x86os_ipc_handle_t endpoint,memory_message_t *message,uint32_t budget) {
    uint64_t start=0,now=0;
    if (x86os_monotonic_ms(&start)) return -1;
    for (unsigned i=0;i<60000;++i) {
        int result=try_receive(endpoint,message);
        if (!result) return 0;
        if (result!=-11 && result!=-13 && result!=-110) return -1;
        if (x86os_monotonic_ms(&now) || now-start>=budget || x86os_sleep_ms(1)) break;
    }
    return -1;
}
static int wait_child(int slot,int expected) {
    uint64_t start=0,now=0;
    if (x86os_monotonic_ms(&start)) return -1;
    for (unsigned round=0;round<10000;++round) {
        for (uint32_t i=0;i<32;++i) {
            x86os_process_info_t info;
            int result=x86os_process_info(i,&info);
            if (result<0) return -1;
            if (!result) break;
            if (info.pid==children[slot] && info.state==X86OS_PROCESS_ZOMBIE) {
                int status=-1;
                if (x86os_wait(children[slot],&status)!=children[slot] || status!=expected) return -1;
                if (x86os_wait(children[slot],&status)>=0) return -1;
                children[slot]=0; return 0;
            }
        }
        if (x86os_monotonic_ms(&now) || now-start>=10000 || x86os_sleep_ms(1)) break;
    }
    return -1;
}
static int spawn_child(int slot,const char *mode,x86os_ipc_handle_t commands,
                       x86os_ipc_handle_t replies) {
    char a[11],b[11]; number(a,commands); number(b,replies);
    const char *argv[]={"/usr/bin/memtest.prg",mode,a,b};
    children[slot]=x86os_spawnv(argv[0],4,argv);
    x86os_process_identity_t identity;
    if (children[slot]<=0) return failed_call("spawn",children[slot]);
    int result=x86os_process_identity_of(children[slot],&identity);
    if (result) return failed_call("identity",result);
    generations[slot]=identity.generation;
    result=x86os_ipc_delegate(commands,children[slot],X86OS_IPC_RIGHT_RECEIVE);
    if (result) return failed_call("delegate-command",result);
    result=x86os_ipc_delegate(replies,children[slot],X86OS_IPC_RIGHT_SEND);
    if (result) return failed_call("delegate-reply",result);
    return send_value(commands,1,0,0);
}
static int worker(const char *mode,x86os_ipc_handle_t commands,x86os_ipc_handle_t replies) {
    if (strcmp(mode,"--peer") && strcmp(mode,"--fault") && strcmp(mode,"--kill")) return 2;
    memory_message_t message;
    if (receive(commands,&message,5000) || message.type!=1) return failed_call("child-start",-1);
    if (strcmp(mode,"--peer")) {
        /* This virtual address belongs to the parent, not to this process. */
        int result=(int)x86os_syscall(X86OS_SYS_FREE,0x40800000U,0,0);
        if (result!=-22) return failed_call("foreign-free",result);
        unsigned char *p=x86os_malloc(16U*MIB);
        if (!p) return failed_call("child-allocation",-12);
        memset(p,0xc3,16U*MIB);
        if (send_value(replies,2,0,0)) return failed_call("child-ready",-1);
        if (receive(commands,&message,30000)) return 1;
        if (!strcmp(mode,"--fault")) { __asm__ volatile("ud2"); return 1; }
        return 1;
    }
    uint64_t start=0,last=0,now=0;
    if (x86os_monotonic_ms(&start) || send_value(replies,2,0,0)) return 1;
    last=start;
    uint32_t ticks=0,maximum=0;
    for (unsigned rounds=0;rounds<60000;++rounds) {
        if (x86os_monotonic_ms(&now) || now<last || now-start>=45000) return 1;
        if (now-last>maximum) maximum=(uint32_t)(now-last);
        last=now; ++ticks;
        int rc=try_receive(commands,&message);
        if (!rc) {
            if (message.type==3) return send_value(replies,4,ticks,maximum) ? 1 : 0;
            if (message.type!=5 || send_value(replies,5,ticks,maximum)) return 1;
        }
        if ((rc && rc!=-11 && rc!=-110) || x86os_sleep_ms(1)) return 1;
    }
    return 1;
}
static int peer_checkpoint(x86os_ipc_handle_t commands,x86os_ipc_handle_t replies,
                           const char *stage) {
    memory_message_t message;
    if (send_value(commands,5,0,0) || receive(replies,&message,5000) || message.type!=5) return -1;
    x86os_puts("PRIVATE_MEMORY_PROGRESS stage="); x86os_puts(stage);
    x86os_puts(" ticks="); x86os_print_number((int)message.a);
    x86os_puts(" max_gap_ms="); x86os_print_number((int)message.b); x86os_puts("\n");
    return 0;
}
static int exercise(void) {
    x86os_memory_stats_t memory,before,after;
    if (x86os_memory_stats(&memory)) return -1;
    uint64_t available=memory.managed_bytes/2U;
    if (available>REIST_LIBC_PROCESS_LIMIT) available=REIST_LIBC_PROCESS_LIMIT;
    uint32_t budget=(uint32_t)available & ~4095U;
    uint32_t big=memory.managed_bytes>=768U*MIB ? 256U*MIB : 16U*MIB;
    if (budget<big+4U*MIB || reist_libc_init_process(budget)) return -1;
    unsigned char *kept=malloc(MIB);
    if (!kept) return -1;
    memset(kept,0x5a,MIB);
    x86os_ipc_handle_t commands=0,replies=0;
    if (x86os_ipc_create(&commands) || x86os_ipc_create(&replies)) return -1;
    memory_message_t message;
    phase="peer-start";
    if (spawn_child(0,"--peer",commands,replies) || receive(replies,&message,5000) || message.type!=2)
        return -1;
    if (x86os_memory_stats(&before)) return -1;
    x86os_puts("PRIVATE_MEMORY_BUDGET bytes="); x86os_print_number((int)budget);
    x86os_puts(" allocation="); x86os_print_number((int)big); x86os_puts("\n");
    phase="large-allocation";
    unsigned char *p=calloc(1,big);
    if (!p) return -1;
    if (peer_checkpoint(commands,replies,"calloc")) return -1;
    for (uint32_t i=0;i<big;i+=4096) {
        if (p[i] || p[i+4095]) return -1;
        p[i]=0xa5; p[i+4095]=0x3c;
    }
    if (malloc(budget) || errno!=ENOMEM || x86os_malloc(budget)) return -1;
    for (uint32_t i=0;i<big;i+=4096) if (p[i]!=0xa5 || p[i+4095]!=0x3c) return -1;
    phase="return-backing";
    free(p);
    if (peer_checkpoint(commands,replies,"free")) return -1;
    if (x86os_memory_stats(&after) || after.allocated_frame_bytes!=before.allocated_frame_bytes)
        return -1;
    x86os_puts("PRIVATE_MEMORY_RETURN_OK\n");
    /* Exact raw backing reuse, no enlargement of a freed historical slot. */
    p=x86os_malloc(8U*MIB);
    if (!p || p[0] || p[8U*MIB-1]) return -1;
    for (uint32_t i=0;i<8U*MIB;i+=4096) { p[i]=0xa5; p[i+4095]=0x3c; }
    if (x86os_realloc(p,budget)) return -1;
    unsigned char *grown=x86os_realloc(p,12U*MIB);
    if (!grown || grown==p) return -1;
    for (uint32_t i=0;i<8U*MIB;i+=4096)
        if (grown[i]!=0xa5 || grown[i+4095]!=0x3c) return -1;
    for (uint32_t i=8U*MIB;i<12U*MIB;i+=4096)
        if (grown[i] || grown[i+4095]) return -1;
    p=x86os_realloc(grown,4U*MIB);
    if (p!=grown) return -1;
    x86os_free(p);
    if (peer_checkpoint(commands,replies,"raw-reuse")) return -1;
    x86os_puts("PRIVATE_MEMORY_REALLOC_OK\n");
    for (unsigned round=0;round<2;++round) {
        phase=round ? "kill-reap" : "fault-reap";
        x86os_ipc_handle_t child_commands=0,child_replies=0;
        int result=x86os_ipc_create(&child_commands);
        if (result) return failed_call("child-endpoint",result);
        result=x86os_ipc_create(&child_replies);
        if (result) return failed_call("child-reply-endpoint",result);
        if (x86os_memory_stats(&before)) return failed_call("child-before-stats",-1);
        if (spawn_child(1,round ? "--kill" : "--fault",child_commands,child_replies)) return -1;
        if (receive(child_replies,&message,5000) || message.type!=2) return failed_call("parent-child-ready",-1);
        if (peer_checkpoint(commands,replies,"child-ready")) return -1;
        for (uint32_t i=0;i<MIB;++i) if (kept[i]!=0x5a) return -1;
        if (round) {
            if (x86os_kill(children[1])) return -1;
        } else if (send_value(child_commands,3,0,0)) return -1;
        if (wait_child(1,round ? 143 : 134)) return failed_call("wait-child",-1);
        if (peer_checkpoint(commands,replies,phase)) return -1;
        if (x86os_ipc_close(child_commands)) return failed_call("close-child-endpoint",-1);
        if (x86os_ipc_close(child_replies)) return failed_call("close-child-reply-endpoint",-1);
        if (x86os_memory_stats(&after)) return failed_call("child-after-stats",-1);
        if (after.allocated_frame_bytes!=before.allocated_frame_bytes)
            return failed_call("child-frame-delta",(int)(after.allocated_frame_bytes-before.allocated_frame_bytes));
        x86os_puts(round ? "PRIVATE_MEMORY_KILL_REAP_OK\n" : "PRIVATE_MEMORY_FAULT_REAP_OK\n");
    }
    phase="peer-progress";
    if (send_value(commands,3,0,0) || receive(replies,&message,5000) || message.type!=4) return -1;
    uint32_t ticks=message.a,gap=message.b;
    x86os_puts("PRIVATE_MEMORY_PEER ticks="); x86os_print_number((int)ticks);
    x86os_puts(" max_gap_ms="); x86os_print_number((int)gap); x86os_puts("\n");
    if (ticks<3 || gap>100 || wait_child(0,0) ||
        x86os_ipc_close(commands) || x86os_ipc_close(replies)) return -1;
    free(kept);
    reist_libc_stats_t stats={REIST_LIBC_VERSION,sizeof(stats),0,0,0,0};
    if (reist_libc_stats(&stats) || stats.capacity || stats.live_objects || reist_libc_reset()) return -1;
    return 0;
}
int main(int argc,char **argv) {
    if (argc==4) return worker(argv[1],decimal(argv[2]),decimal(argv[3]));
    if (argc!=1) return 2;
    if (exercise()) {
        x86os_puts("PRIVATE_MEMORY_RUNTIME_FAIL\n");
        x86os_puts("PRIVATE_MEMORY_PHASE "); x86os_puts(phase); x86os_puts("\n");
        for (unsigned i=0;i<2;++i) if (children[i]>0) {
            x86os_process_identity_t id;
            if (!x86os_process_identity_of(children[i],&id) && id.generation==generations[i]) {
                (void)x86os_kill(children[i]); (void)wait_child((int)i,143);
            }
        }
        return 1;
    }
    x86os_puts("PRIVATE_MEMORY_RUNTIME_OK\n"); return 0;
}
