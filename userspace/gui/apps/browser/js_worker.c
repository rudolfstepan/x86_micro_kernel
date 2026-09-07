/* Persistent, separately owned JS realm. No VFS, network, GUI or DOM API. */
#include "js_protocol.h"
#include "x86os.h"
#include <reist_js.h>
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
static int run(uint32_t incoming,uint32_t outgoing,js_service_header identity,uint32_t probe) {
    uint64_t last=0,now=0;
    if(monotonic(&last,&now) || now>UINT64_MAX-JS_SERVICE_DEADLINE) return 74;
    uint64_t startup_deadline=now+JS_SERVICE_DEADLINE;
    char *input=malloc(JS_SERVICE_SOURCE),*output=malloc(JS_SERVICE_RESULT);
    reist_js_engine *engine=NULL;
    int result=74; uint32_t sequence=0,document=0,evaluations=0;
    if(!input || !output) { result=71; goto done; }
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
            if(js_service_accept(&packet,message.length,&request,0,input,JS_SERVICE_SOURCE,&received)) goto done;
            if(received.offset==received.total) break;
        }
        ++sequence; document=request.document;
        if(monotonic(&last,&now) || now>=request.deadline) goto done;
        reist_js_status status=REIST_JS_OK; const void *reply=NULL; uint32_t size=0;
        uint32_t hello[]={JS_SERVICE_VERSION,JS_SERVICE_HEAP,16384,0};
        reist_js_stats stats={REIST_JS_VERSION,sizeof(stats),0,0,0,0};
        if(request.operation==JS_OP_HELLO) {
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
            /* Explicit fixture argument only. First eval establishes a living
             * realm with an 8-MiB buffer; second eval enters the fault. */
            if(evaluations==1 && probe==1) {
                x86os_puts("JS_SERVICE_FAULT_ENTERED\n");
                volatile uintptr_t address=4; *(volatile uint32_t *)address=1;
                goto done;
            }
            if(evaluations==1 && probe==2) {
                x86os_puts("JS_SERVICE_HANG_ENTERED\n");
                for(;;) __asm__ volatile("" ::: "memory");
            }
            size_t required=0;
            status=reist_js_eval(engine,input,received.total,request.deadline,output,JS_SERVICE_RESULT,&required);
            if(!status) { reply=output; size=(uint32_t)required; }
            if(evaluations==1 && probe==3) { ++request.child_generation; x86os_puts("JS_SERVICE_STALE_ENTERED\n"); }
            ++evaluations;
        } else if(request.operation==JS_OP_GC || request.operation==JS_OP_HEALTH) {
            if(request.operation==JS_OP_GC) status=reist_js_collect(engine,request.deadline);
            if(!status && reist_js_get_stats(engine,&stats)) goto done;
            if(!status) { reply=&stats; size=sizeof(stats); }
        }
        if(send_reply(outgoing,&request,status,reply,size,&last)) goto done;
        if(request.operation==JS_OP_SHUTDOWN || status>REIST_JS_EXCEPTION) { result=0; break; }
    }
done:
    reist_js_destroy(&engine); free(input); free(output);
    if(reist_libc_reset()) result=70;
    return result;
}
int main(int argc,char **argv) {
    if(argc!=6) return 64;
    uint32_t incoming=number(argv[1]),outgoing=number(argv[2]),parent=number(argv[3]),generation=number(argv[4]);
    uint32_t probe=number(argv[5]);
    if(!incoming || !outgoing || incoming==outgoing || !parent || !generation || probe>3 ||
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
    (void)x86os_ipc_close(incoming); (void)x86os_ipc_close(outgoing);
    return result;
}
