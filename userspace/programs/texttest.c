/* Opt-in string formatter consumer and bounded generation-scoped Ring-3 fault proof. */
#include <x86os.h>
#include <stdio.h>
#include "../../test/text_vectors.h"

static int equal(const char *a,const char *b) {
    for (unsigned i=0;i<32;++i) { if(a[i]!=b[i]) return 0; if(!a[i]) return 1; }
    return 0;
}
static void decimal(char out[11],uint32_t value) {
    char reverse[10]; unsigned n=0;
    do { reverse[n++]=(char)('0'+value%10); value/=10; } while(value && n<10);
    for(unsigned i=0;i<n;++i) out[i]=reverse[n-i-1];
    out[n]=0;
}
static uint32_t parse(const char *s) {
    uint32_t value=0;
    for(unsigned i=0;i<10;++i) {
        if(!s[i]) return value;
        if(s[i]<'0'||s[i]>'9'||value>(UINT32_MAX-(unsigned)(s[i]-'0'))/10) return 0;
        value=value*10+(unsigned)(s[i]-'0');
    }
    return s[10] ? 0 : value;
}
static int send(x86os_ipc_handle_t endpoint) {
    x86os_ipc_message_t msg={X86OS_IPC_MESSAGE_VERSION,sizeof(msg),1,{0x4d}};
    return x86os_ipc_send_timeout(endpoint,&msg,1000);
}
static int receive(x86os_ipc_handle_t endpoint,uint32_t timeout) {
    x86os_ipc_message_t msg={X86OS_IPC_MESSAGE_VERSION,sizeof(msg),0,{0}};
    int result=x86os_ipc_receive_timeout(endpoint,&msg,timeout);
    return result ? result : (msg.version!=X86OS_IPC_MESSAGE_VERSION ||
        msg.struct_size!=sizeof(msg)||msg.length!=1||msg.payload[0]!=0x4d ? -22 : 0);
}
static int defaults(void) {
    uint16_t control; uint32_t simd;
    __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(control),"=m"(simd));
    return control==0x037f && simd==0x1f80 && !fetestexcept(FE_ALL_EXCEPT);
}
static int exercise(void) {
    int line=text_vectors();
    if(line) {
        x86os_puts("TEXT_TEST_FAIL line="); x86os_print_number(line); x86os_puts("\n");
    }
    return line;
}
static int child(const char *mode,x86os_ipc_handle_t command,x86os_ipc_handle_t reply) {
    if(!command || !reply || !defaults()) return 91;
    uint64_t start=0,now=0;
    if(x86os_monotonic_ms(&start)) return 92;
    int ready=0;
    for(unsigned i=0;i<5000;++i) {
        int result=receive(command,0);
        if(!result) { ready=1; break; }
        if(result!=-11 && result!=-13 && result!=-110) return 92;
        if(x86os_monotonic_ms(&now)||now<start||now-start>=5000||x86os_sleep_ms(1)) break;
    }
    if(!ready || exercise() || send(reply) || receive(command,5000)) return 93;
    if(equal(mode,"--normal")) return 37;
    if(equal(mode,"--fault")) {
        char output[8]; volatile uintptr_t invalid=4;
        (void)snprintf(output,sizeof output,"%s",(const char *)invalid);
        return 94;
    }
    if(equal(mode,"--hold")) {
        errno=EILSEQ;
        if(fesetround(FE_DOWNWARD)) return 95;
        x86os_sleep_ms(5000); return 96;
    }
    return 97;
}
static int owned(int pid,uint32_t generation,int allow_zombie) {
    x86os_process_identity_t identity;
    int result=x86os_process_identity_of(pid,&identity);
    if(result) return allow_zombie && result==-3;
    return identity.generation==generation;
}
static int observe(int pid,uint32_t generation,int wanted,uint32_t budget) {
    uint64_t start=0,now=0;
    if(x86os_monotonic_ms(&start)) return -1;
    for(unsigned round=0;round<10000;++round) {
        if(!owned(pid,generation,wanted==X86OS_PROCESS_ZOMBIE)) return -1;
        for(unsigned i=0;i<32;++i) {
            x86os_process_info_t info;
            int result=x86os_process_info(i,&info);
            if(result<0) return -1;
            if(!result) break;
            if(info.pid==pid) {
                if(info.parent_pid!=x86os_getpid()) return -1;
                if(info.state==wanted) return 0;
                if(info.state==X86OS_PROCESS_ZOMBIE) return -1;
            }
        }
        if(x86os_monotonic_ms(&now)||now<start||now-start>=budget||x86os_sleep_ms(1)) break;
    }
    return -1;
}
static int reap(int pid,uint32_t generation,int expected) {
    int status=-1;
    x86os_process_identity_t identity;
    return observe(pid,generation,X86OS_PROCESS_ZOMBIE,10000) ||
        x86os_wait(pid,&status)!=pid || status!=expected || x86os_wait(pid,&status)>=0 ||
        x86os_process_identity_of(pid,&identity)>=0 ? -1 : 0;
}
static int containment(void) {
    static const char *const modes[]={"--normal","--fault","--hold","--normal"};
    static const int statuses[]={37,142,143,37};
    int previous_pid=0; uint32_t previous_generation=0;
    if(fesetround(FE_UPWARD)) return -1;
    errno=61;
    for(unsigned round=0;round<4;++round) {
        x86os_ipc_handle_t command=0,reply=0;
        x86os_process_identity_t identity;
        uint32_t generation=0; int pid=0,ok=0;
        do {
            if(x86os_ipc_create(&command)||x86os_ipc_create(&reply)) break;
            char a[11],b[11]; decimal(a,command); decimal(b,reply);
            const char *args[]={"/usr/bin/texttest.prg",modes[round],a,b};
            pid=x86os_spawnv(args[0],4,args);
            if(pid<=0||x86os_process_identity_of(pid,&identity)||!identity.generation) break;
            generation=identity.generation;
            if(pid==previous_pid && generation==previous_generation) break;
            previous_pid=pid; previous_generation=generation;
            if(x86os_ipc_delegate(command,pid,X86OS_IPC_RIGHT_RECEIVE)||
               x86os_ipc_delegate(reply,pid,X86OS_IPC_RIGHT_SEND)||send(command)||
               receive(reply,5000)||send(command)) break;
            if(round==2 && (observe(pid,generation,X86OS_PROCESS_SLEEPING,2000)||
                !owned(pid,generation,0)||x86os_kill(pid))) break;
            if(reap(pid,generation,statuses[round])) break;
            pid=0;
            uint16_t control; uint32_t simd;
            __asm__ volatile("fnstcw %0; stmxcsr %1" : "=m"(control),"=m"(simd));
            if(control!=(0x037f|FE_UPWARD)||simd!=(0x1f80|(FE_UPWARD<<3))||fegetround()!=FE_UPWARD||errno!=61) break;
            ok=1;
        } while(0);
        if(pid>0 && generation && owned(pid,generation,1)) {
            if(owned(pid,generation,0)) {
                if(!x86os_kill(pid)) (void)reap(pid,generation,143);
            } else {
                /* A child that faulted before its reply is already fenced;
                 * wait still checks this parent, and consumes its zombie. */
                int abandoned_status=-1;
                (void)x86os_wait(pid,&abandoned_status);
            }
        }
        if(reply && x86os_ipc_close(reply)) ok=0;
        if(command && x86os_ipc_close(command)) ok=0;
        if(!ok) return -1;
        x86os_puts("TEXT_REAP_OK mode="); x86os_puts(modes[round]);
        x86os_puts(" status="); x86os_print_number(statuses[round]);
        x86os_puts(" pid="); x86os_print_number(previous_pid);
        x86os_puts(" generation="); x86os_print_number((int)generation); x86os_puts("\n");
    }
    return fesetround(FE_TONEAREST);
}
int main(int argc,char **argv) {
    if(argc==4) return child(argv[1],parse(argv[2]),parse(argv[3]));
    if(argc!=1) return 2;
    if(!defaults() || exercise()) { x86os_puts("TEXT_TEST_FAIL initial\n"); return 1; }
    x86os_puts("TEXT_VECTORS_OK\n");
    if(containment()) { x86os_puts("TEXT_TEST_FAIL containment\n"); return 1; }
    x86os_puts("TEXT_PARENT_OK\nTEXT_RUNTIME_OK\n"); return 0;
}
