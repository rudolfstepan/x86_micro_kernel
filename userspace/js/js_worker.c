/* Shared isolated worker. No ambient VFS, network, GUI or DOM authority. */
#include "js_protocol.h"
#include "script_protocol.h"
#include "x86os.h"
#include <reist_js.h>
#include <reist_js_script.h>
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
static int monotonic(void *context,uint64_t *now) {
    uint64_t *last=context;
    if(x86os_monotonic_ms(now) || *now<*last) return -1;
    *last=*now; return 0;
}
static uint32_t number(const char *s) {
    uint32_t n=0;
    for(unsigned i=0;i<10;++i) {
        if(!s[i]) return n;
        if(s[i]<'0' || s[i]>'9' || n>(UINT32_MAX-(uint32_t)(s[i]-'0'))/10) return 0;
        n=n*10+(uint32_t)(s[i]-'0');
    }
    return s[10] ? 0 : n;
}
static int parent_alive(const js_service_header *h) {
    x86os_process_identity_t parent;
    return !x86os_process_identity_of((int)h->parent_pid,&parent) && parent.version==1 &&
        parent.struct_size==sizeof(parent) && parent.pid==(int)h->parent_pid && parent.generation==h->parent_generation;
}
static int send_reply(uint32_t endpoint,const js_service_header *header,uint32_t status,
    const void *bytes,uint32_t size,uint64_t *last) {
    uint32_t offset=0;
    do {
        uint64_t now=0;
        if(monotonic(last,&now) || now>=header->deadline || !parent_alive(header)) return -1;
        js_service_packet packet;
        js_service_packet_make(&packet,header,status,bytes,size,offset);
        x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),
            JS_SERVICE_HEADER+packet.header.length,{0}};
        memcpy(message.payload,&packet,message.length);
        if(x86os_ipc_send_bulk_timeout(endpoint,&message,(uint32_t)(header->deadline-now))) return -1;
        offset+=packet.header.length;
    } while(offset<size);
    return 0;
}
/* Explicit native fixture only, after both directed grants and before HELLO.
 * These are real INT80 calls, not tests for missing JS globals. */
static int restricted_probe(uint32_t incoming,uint32_t outgoing) {
    static const uint8_t allowed[]={4,5,6,9,22,26,40,41,42,53,54,58,114,128};
    for(uint32_t call=0;call<REIST_SYSCALL_COUNT;++call) {
        int permitted=0;
        for(unsigned i=0;i<sizeof(allowed);++i) if(call==allowed[i]) permitted=1;
        if(!permitted && (int)x86os_syscall(call,4,4,4)!=-13) return -1;
    }
    if((int)x86os_syscall(REIST_SYSCALL_COUNT,4,4,4)!=-13 ||
       (int)x86os_syscall(UINT32_MAX,4,4,4)!=-13) return -1;
    reist_process_restrict_request_t request={1,sizeof(request),1,0};
    for(unsigned i=0;i<4;++i) {
        reist_process_restrict_request_t bad=request;
        if(i==0) bad.version=0;
        if(i==1) --bad.struct_size;
        if(i==2) bad.profile=0; /* no ambient/Compatibility escape */
        if(i==3) bad.reserved=1;
        if((int)x86os_syscall(X86OS_SYS_PROCESS_RESTRICT,(uintptr_t)&bad,0,0)!=-22) return -1;
    }
    if((int)x86os_syscall(X86OS_SYS_PROCESS_RESTRICT,4,0,0)!=-14 ||
       x86os_process_restrict_script()) return -1;
    x86os_process_info_t info;
    if(x86os_process_info(0,&info)!=1 || info.pid!=x86os_getpid() ||
       x86os_process_info(1,&info)!=0) return -1;
    x86os_process_identity_t identity;
    if(x86os_process_identity_of(2147483647,&identity)!=-13) return -1;
    x86os_ipc_message_t message={X86OS_IPC_MESSAGE_VERSION,sizeof(message),0,{0}};
    if(x86os_ipc_send_timeout(incoming,&message,0)!=-13 ||
       x86os_ipc_receive_timeout(outgoing,&message,0)!=-13 ||
       x86os_ipc_send_timeout(0,&message,0)!=-9 ||
       x86os_ipc_release(0)!=-22 || x86os_ipc_release(UINT32_MAX)!=-9) return -1;
    if(x86os_malloc(64U*1024U*1024U+4096U)) return -1;
    uint32_t *small=x86os_malloc(4096);
    if(!small) return -1;
    *small=0x334;
    uint32_t *grown=x86os_realloc(small,8192);
    if(!grown) { x86os_free(small); return -1; }
    int good=*grown==0x334;
    if((int)x86os_syscall(X86OS_SYS_FREE,(uintptr_t)grown,0,0) || !good) return -1;
    return 0;
}
static int run(uint32_t incoming,uint32_t outgoing,js_service_header identity,uint32_t probe) {
    uint64_t last=0,now=0;
    if(monotonic(&last,&now) || now>UINT64_MAX-JS_SERVICE_DEADLINE) return 74;
    uint64_t startup_deadline=now+JS_SERVICE_DEADLINE;
    char *input=malloc(JS_SERVICE_SCRIPT_SOURCE),*output=malloc(JS_SERVICE_RESULT);
    char *console=malloc(JS_SCRIPT_CONSOLE+JS_SCRIPT_HEADER);
    reist_js_script_host host={1,sizeof(host),console?console+JS_SCRIPT_HEADER:NULL,
        JS_SCRIPT_CONSOLE,0,0,0,0,0};
    reist_js_engine *engine=NULL;
    int result=74; uint32_t sequence=0,document=0,evaluations=0;
    if(!input || !output || !console) { result=71; goto done; }
    for(;;) {
        js_service_receive received={0,UINT32_MAX,UINT32_MAX};
        js_service_header request=identity;
        for(;;) {
            if(monotonic(&last,&now) || !parent_alive(&identity)) goto done;
            uint32_t timeout=1000;
            if(!engine || received.total!=UINT32_MAX) {
                uint64_t deadline=received.total==UINT32_MAX ? startup_deadline : request.deadline;
                if(now>=deadline) goto done;
                if(deadline-now<timeout) timeout=(uint32_t)(deadline-now);
            }
            x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),0,{0}};
            int rc=x86os_ipc_receive_bulk_timeout(incoming,&message,timeout);
            if(!engine && received.total==UINT32_MAX && (rc==-9 || rc==-13)) {
                if(x86os_sleep_ms(1)) goto done;
                continue;
            }
            if(rc==-110 || rc==-11) continue;
            if(rc || message.version!=X86OS_IPC_BULK_MESSAGE_VERSION || message.struct_size!=sizeof(message) ||
               message.length<JS_SERVICE_HEADER || message.length>sizeof(js_service_packet)) goto done;
            js_service_packet packet; memcpy(&packet,message.payload,sizeof(packet));
            if(received.total==UINT32_MAX) {
                if(sequence==UINT32_MAX || packet.header.sequence!=sequence+1 ||
                   (!engine && packet.header.operation!=JS_OP_HELLO) ||
                   (engine && packet.header.operation==JS_OP_HELLO) ||
                   (engine && packet.header.document!=document) || monotonic(&last,&now) ||
                   packet.header.deadline<=now || packet.header.deadline-now>JS_SERVICE_DEADLINE) goto done;
                request.operation=packet.header.operation; request.sequence=sequence+1;
                request.document=engine ? document : packet.header.document;
                request.deadline=packet.header.deadline;
            }
            if(js_service_accept(&packet,message.length,&request,0,input,JS_SERVICE_SCRIPT_SOURCE,&received)) goto done;
            if(received.offset==received.total) break;
        }
        ++sequence; document=request.document;
        if(monotonic(&last,&now) || now>=request.deadline) goto done;
        reist_js_status status=REIST_JS_OK; const void *reply=NULL; uint32_t size=0;
        uint32_t hello[]={JS_SERVICE_VERSION,JS_SERVICE_HEAP,16384,0};
        reist_js_stats stats={REIST_JS_VERSION,sizeof(stats),0,0,0,0};
        if(request.operation==JS_OP_HELLO) {
            if(probe==4 && restricted_probe(incoming,outgoing)) goto done;
            uint64_t seed; memcpy(&seed,input,sizeof(seed));
            reist_js_config config={REIST_JS_VERSION,sizeof(config),JS_SERVICE_HEAP,16384,
                JS_SERVICE_SOURCE,JS_SERVICE_RESULT,1024,0,seed,&last,monotonic};
            engine=reist_js_create(&config,&status);
            if(!engine) goto done;
            size_t required=0;
            status=reist_js_eval(engine,"6*7",3,request.deadline,output,JS_SERVICE_RESULT,&required);
            if(status || required!=2 || memcmp(output,"42",3)) goto done;
            reply=hello; size=sizeof(hello);
        } else if(request.operation==JS_OP_EVAL) {
            if(host.records || host.failed || host.used || host.exit_code || evaluations==UINT32_MAX) goto done;
            /* Explicit fixture argument only. First eval establishes a living
             * realm with an 8-MiB buffer; second eval enters the fault. */
            if(evaluations==1 && probe==1) {
                volatile uintptr_t address=4; *(volatile uint32_t *)address=1;
                goto done;
            }
            if(evaluations==1 && probe==2) {
                /* Confirm the native branch through the delegated channel;
                 * parent then submits another request and proves its timeout. */
                if(send_reply(outgoing,&request,REIST_JS_OK,"native-hang",11,&last)) goto done;
                for(;;) __asm__ volatile("" ::: "memory");
            }
            size_t required=0;
            status=reist_js_eval(engine,input,received.total,request.deadline,output,JS_SERVICE_RESULT,&required);
            if(!status) { reply=output; size=(uint32_t)required; }
            if(evaluations==1 && probe==3) ++request.child_generation;
            ++evaluations;
        } else if(request.operation==JS_OP_SCRIPT) {
            js_script_source source;
            if(evaluations || js_script_decode(input,received.total,&source)) goto done;
            /* Never mix CLI host state with browser EVAL on this generation. */
            evaluations=UINT32_MAX;
            status=reist_js_script_attach(engine,&host,source.argc,source.argv);
            size_t required=0;
            if(!status) status=reist_js_eval(engine,source.source,source.source_bytes,
                request.deadline,output,JS_SERVICE_RESULT,&required);
            if(status<=REIST_JS_EXCEPTION && !host.failed) {
                js_script_reply journal={1,sizeof(journal),status,status?1:host.exit_code,host.records,host.used};
                memcpy(console,&journal,sizeof(journal)); reply=console; size=sizeof(journal)+host.used;
                status=REIST_JS_OK;
            }
        } else if(request.operation==JS_OP_GC || request.operation==JS_OP_HEALTH) {
            if(request.operation==JS_OP_GC) status=reist_js_collect(engine,request.deadline);
            if(!status && reist_js_get_stats(engine,&stats)) goto done;
            if(!status) { reply=&stats; size=sizeof(stats); }
        }
        if(send_reply(outgoing,&request,status,reply,size,&last)) goto done;
        if(request.operation==JS_OP_SHUTDOWN || status>REIST_JS_EXCEPTION) { result=0; break; }
    }
done:
    reist_js_destroy(&engine); free(input); free(output); free(console);
    if(reist_libc_reset()) result=70;
    return result;
}
int main(int argc,char **argv) {
    if(x86os_process_restrict_script()) return 70;
    if(argc!=6) return 64;
    uint32_t incoming=number(argv[1]),outgoing=number(argv[2]),parent=number(argv[3]),generation=number(argv[4]);
    uint32_t probe=number(argv[5]);
    if(!incoming || !outgoing || incoming==outgoing || !parent || !generation || probe>4 ||
       (probe==0 && strcmp(argv[5],"0"))) return 64;
    x86os_process_identity_t self;
    if(x86os_process_identity_of(x86os_getpid(),&self) || self.version!=1 || self.struct_size!=sizeof(self) ||
       self.pid!=x86os_getpid() || !self.generation) return 70;
    int owned=0;
    for(unsigned i=0;i<32;++i) {
        x86os_process_info_t info; int rc=x86os_process_info(i,&info);
        if(rc<=0) break;
        if(info.pid==self.pid) { owned=info.parent_pid==(int)parent; break; }
    }
    if(!owned || reist_libc_init_process(64U*1024U*1024U)) return 70;
    js_service_header identity={0};
    identity.magic=JS_SERVICE_MAGIC; identity.version=JS_SERVICE_VERSION; identity.size=JS_SERVICE_HEADER;
    identity.parent_pid=parent; identity.parent_generation=generation;
    identity.child_pid=(uint32_t)self.pid; identity.child_generation=self.generation;
    int result=run(incoming,outgoing,identity,probe);
    (void)x86os_ipc_release(incoming); (void)x86os_ipc_release(outgoing);
    return result;
}
