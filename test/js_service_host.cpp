#include "js_protocol.h"
#include "js_session.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
using reist::browser::JsSession;
#define REQUIRE(x) do { if(!(x)) { std::printf("JS_SERVICE_HOST_FAIL line=%u\n",__LINE__); std::exit(1); } } while(0)
static struct {
    uint64_t now=100;
    int child=0,state=0,status=0,spawns=0,kills=0,waits=0,closed=0,calls=0;
    uint32_t generation=0,endpoint=10;
    bool blocked=false,waiting=false,stale=false,hold_kill=false,clock_error=false,delegate_error=false;
    uint32_t response_status=0;
    uint32_t response_size=2,response_offset=0;
    js_service_header last{};
    bool file_call=false,file_sent=false;
    uint32_t file_request[8]={1,32,1,1,1,1,3,0};
} os;
extern "C" {
int x86os_getpid() { return 7; }
int x86os_monotonic_ms(uint64_t *n) { *n=os.now; return os.clock_error?-5:0; }
int x86os_process_identity_of(int pid,x86os_process_identity_t *p) {
    if(pid==7) { *p={1,sizeof(*p),7,3}; return 0; }
    if(pid!=os.child || os.state==X86OS_PROCESS_ZOMBIE) return -3;
    *p={1,sizeof(*p),pid,os.generation}; return 0;
}
int x86os_process_info(uint32_t index,x86os_process_info_t *p) {
    if(index || !os.child) return 0;
    *p={}; p->pid=os.child; p->parent_pid=7; p->state=os.state; return 1;
}
int x86os_ipc_create(x86os_ipc_handle_t *h) { *h=++os.endpoint; return 0; }
int x86os_ipc_close(x86os_ipc_handle_t) { ++os.closed; return 0; }
int x86os_ipc_delegate(x86os_ipc_handle_t,int pid,uint32_t rights) {
    REQUIRE(pid==os.child); REQUIRE(rights==X86OS_IPC_RIGHT_SEND || rights==X86OS_IPC_RIGHT_RECEIVE);
    return os.delegate_error?-13:0;
}
int x86os_spawnv(const char *path,int argc,const char *const *) {
    REQUIRE(!std::strcmp(path,"/usr/bin/jswork.prg") && argc==6 && !os.child);
    os.child=9; ++os.generation; ++os.spawns; os.state=X86OS_PROCESS_RUNNING; os.status=0;
    os.waiting=false; return os.child;
}
int x86os_kill(int pid) {
    REQUIRE(pid==os.child && os.closed>=2); ++os.kills;
    if(!os.hold_kill) { os.state=X86OS_PROCESS_ZOMBIE; os.status=143; }
    return 0;
}
int x86os_wait(int pid,int *status) {
    REQUIRE(pid==os.child && os.state==X86OS_PROCESS_ZOMBIE);
    ++os.waits; *status=os.status; os.child=0; return pid;
}
int x86os_ipc_send_bulk_timeout(x86os_ipc_handle_t,const x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    REQUIRE(timeout==0); ++os.calls;
    if(os.blocked) return -11;
    js_service_packet p; std::memcpy(&p,m->payload,sizeof(p));
    if(p.header.operation==JS_OP_FILE) {
        js_service_header expected=os.last;expected.operation=JS_OP_FILE;
        REQUIRE(!js_service_header_valid(&p.header,&expected,1));
        if(p.header.offset+p.header.length==p.header.total){os.file_call=false;os.waiting=true;os.response_offset=0;}
        return 0;
    }
    REQUIRE(js_service_header_valid(&p.header,&p.header,0)==0);
    os.last=p.header;
    os.waiting=p.header.offset+p.header.length==p.header.total;
    if(os.waiting) os.response_offset=0;
    return 0;
}
int x86os_ipc_receive_bulk_timeout(x86os_ipc_handle_t,x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    REQUIRE(timeout==0); ++os.calls;
    if(os.blocked || !os.waiting) return -11;
    static char big[60000]; std::memset(big,'x',sizeof(big));
    const void *data=os.response_size==2 ? (const void *)"42" : big; uint32_t size=os.response_size;
    uint32_t hello[]={1,JS_SERVICE_HEAP,16384,0},stats[]={1,24,10,4096,8192,0};
    uint32_t script[]={1,24,0,7,0,0};
    if(os.last.operation==JS_OP_SCRIPT || os.last.operation==JS_OP_CAP_SCRIPT) { data=script; size=sizeof(script); }
    if(os.last.operation==JS_OP_HELLO) { data=hello; size=sizeof(hello); }
    if(os.last.operation==JS_OP_GC || os.last.operation==JS_OP_HEALTH) { data=stats; size=sizeof(stats); }
    if(os.last.operation==JS_OP_SHUTDOWN) { data=nullptr; size=0; os.state=X86OS_PROCESS_ZOMBIE; }
    if(os.response_status) { data=nullptr; size=0; }
    js_service_packet p; js_service_packet_make(&p,&os.last,os.response_status,data,size,os.response_offset);
    if(os.file_call) {
        if(os.file_sent)return -11;
        js_service_header h=os.last;h.operation=JS_OP_FILE;
        js_service_packet_make(&p,&h,JS_SERVICE_REQUEST,os.file_request,sizeof(os.file_request),0);
        os.file_sent=true;
    }
    if(os.stale) ++p.header.child_generation;
    m->version=X86OS_IPC_BULK_MESSAGE_VERSION; m->struct_size=sizeof(*m);
    m->length=JS_SERVICE_HEADER+p.header.length; std::memcpy(m->payload,&p,m->length);
    os.response_offset+=p.header.length; os.waiting=os.response_offset<size; return 0;
}
}
static void protocol() {
    js_service_header q{}; q.magic=JS_SERVICE_MAGIC; q.version=1; q.size=JS_SERVICE_HEADER;
    q.operation=JS_OP_HELLO; q.parent_pid=7; q.parent_generation=3;
    q.child_pid=9; q.child_generation=1; q.document=6; q.sequence=1; q.deadline=500;
    uint64_t seed=42; js_service_packet p;
    js_service_packet_make(&p,&q,JS_SERVICE_REQUEST,&seed,8,0);
    for(unsigned field=0;field<18;++field) {
        js_service_packet bad=p;
        uint32_t word; std::memcpy(&word,(char *)&bad.header+4*field,4); word^=UINT32_MAX;
        std::memcpy((char *)&bad.header+4*field,&word,4);
        char out[32]; std::memset(out,0x5a,sizeof(out));
        js_service_receive state{0,UINT32_MAX,UINT32_MAX},before=state;
        REQUIRE(js_service_accept(&bad,80,&q,0,out,sizeof(out),&state)<0);
        REQUIRE(!std::memcmp(&state,&before,sizeof(state)));
        for(char c:out) REQUIRE(c==0x5a);
    }
    for(unsigned length=0;length<80;++length) {
        uint64_t out=0; js_service_receive state{0,UINT32_MAX,UINT32_MAX};
        REQUIRE(js_service_accept(&p,length,&q,0,&out,8,&state)<0 && !out);
    }
    uint64_t out=0; js_service_receive state{0,UINT32_MAX,UINT32_MAX};
    REQUIRE(!js_service_accept(&p,80,&q,0,&out,8,&state) && out==seed);
    REQUIRE(js_service_accept(&p,80,&q,0,&out,8,&state)<0);
    static char large[JS_SERVICE_SOURCE],copy[JS_SERVICE_SOURCE];
    for(unsigned i=0;i<sizeof(large);++i) large[i]=char(i%251);
    q.operation=JS_OP_EVAL; state={0,UINT32_MAX,UINT32_MAX};
    while(state.offset<sizeof(large)) {
        js_service_packet_make(&p,&q,JS_SERVICE_REQUEST,large,sizeof(large),state.offset);
        if(state.offset) {
            js_service_packet bad=p; --bad.header.offset;
            auto before=state;
            REQUIRE(js_service_accept(&bad,72+bad.header.length,&q,0,copy,sizeof(copy),&state)<0);
            REQUIRE(!std::memcmp(&before,&state,sizeof(state)));
        }
        REQUIRE(!js_service_accept(&p,72+p.header.length,&q,0,copy,sizeof(copy),&state));
    }
    REQUIRE(!std::memcmp(copy,large,sizeof(copy)));
    q.operation=JS_OP_SHUTDOWN; state={0,UINT32_MAX,UINT32_MAX};
    js_service_packet_make(&p,&q,JS_SERVICE_REQUEST,nullptr,0,0);
    REQUIRE(!js_service_accept(&p,72,&q,0,nullptr,0,&state));
    REQUIRE(js_service_accept(&p,72,&q,0,nullptr,0,&state)<0);
}
static void pump(JsSession &s) {
    for(unsigned i=0;i<2000 && s.busy();++i) {
        int before=os.calls; s.poll(); REQUIRE(os.calls-before<=8);
    }
    REQUIRE(!s.busy());
}
static void sessions() {
    JsSession s; char output[64]; static char large[JS_SERVICE_SOURCE];
    REQUIRE(!s.start(1,42)); pump(s); REQUIRE(s.ready() && os.spawns==1);
    REQUIRE(s.start(1,42)<0); REQUIRE(!s.evaluate("6*7",3,output,sizeof(output)));
    REQUIRE(s.evaluate("1",1,output,sizeof(output))<0);
    pump(s); REQUIRE(s.result() && !std::strcmp(s.result(),"42"));
    REQUIRE(!s.evaluate(large,sizeof(large),output,sizeof(output)));
    os.blocked=true; int before=os.calls; s.poll(); REQUIRE(os.calls-before==1 && s.busy() && !s.result());
    os.blocked=false; pump(s); REQUIRE(s.result_length()==2);
    static char big_reply[JS_SERVICE_RESULT]; os.response_size=60000;
    REQUIRE(!s.evaluate("1",1,big_reply,sizeof(big_reply)));
    before=os.calls; s.poll(); REQUIRE(os.calls-before==8 && !s.result());
    pump(s); REQUIRE(s.result_length()==60000 && big_reply[59999]=='x' && !big_reply[60000]); os.response_size=2;
    REQUIRE(!s.health(true)); pump(s); REQUIRE(s.stats() && s.stats()[3]==4096);
    REQUIRE(!s.shutdown()); pump(s); REQUIRE(s.state()==JsSession::State::closed && s.exit_status()==0 && os.waits==1 && !os.kills);
    REQUIRE(s.start(1,42)<0);
    REQUIRE(!s.start(2,42)); pump(s);
    REQUIRE(!s.evaluate("1",1,output,sizeof(output))); os.response_status=1;
    pump(s); REQUIRE(s.ready() && s.engine_status()==1 && !s.result()); os.response_status=0;
    REQUIRE(!s.evaluate("1",1,output,sizeof(output))); os.stale=true;
    pump(s); REQUIRE(s.state()==JsSession::State::failed && !s.result() && s.exit_status()==143); os.stale=false;
    REQUIRE(!s.start(2,42)); pump(s); REQUIRE(s.ready()); // Recovery #1
    REQUIRE(!s.evaluate("1",1,output,sizeof(output))); os.response_status=2;
    pump(s); REQUIRE(s.state()==JsSession::State::failed && s.engine_status()==2); os.response_status=0;
    REQUIRE(!s.start(2,42)); pump(s); REQUIRE(s.ready()); // Recovery #2
    REQUIRE(!s.evaluate("1",1,output,sizeof(output),20)); os.now+=20;
    pump(s); REQUIRE(s.state()==JsSession::State::failed && s.error()==-110);
    before=os.spawns; REQUIRE(s.start(2,42)==-28 && os.spawns==before);
    REQUIRE(!s.start(3,42)); pump(s); REQUIRE(s.ready());
    os.now--; s.poll(); pump(s); REQUIRE(s.state()==JsSession::State::failed && !s.result()); os.now++;
    os.delegate_error=true; REQUIRE(s.start(4,42)<0); pump(s);
    REQUIRE(s.state()==JsSession::State::failed && !os.child); os.delegate_error=false;
    REQUIRE(!s.start(5,42)); pump(s);
    REQUIRE(!s.evaluate("1",1,output,sizeof(output))); pump(s);
    s.cancel(); pump(s);
    REQUIRE(!std::strcmp(output,"42")); // Published caller storage is no longer writable by owner cleanup.
    REQUIRE(s.state()==JsSession::State::failed && s.error()==-125);
    REQUIRE(!s.start(6,42)); pump(s); os.clock_error=true; s.poll(); pump(s);
    REQUIRE(s.state()==JsSession::State::failed && s.error()==-84); os.clock_error=false;
}
static void terminal_ownership() {
    // These fixtures deliberately end in an unrecoverable owner state. No
    // destructor/reuse is permitted; storage dies with this host test process.
    alignas(JsSession) static unsigned char lost_bytes[sizeof(JsSession)],late_bytes[sizeof(JsSession)];
    auto *lost=new(lost_bytes) JsSession;
    REQUIRE(!lost->start(1,42)); pump(*lost);
    int kills=os.kills; ++os.generation; lost->poll();
    REQUIRE(lost->state()==JsSession::State::stranded && os.kills==kills && !lost->result());
    REQUIRE(lost->start(2,42)<0);
    os.child=0; // Test model retires the reused, unrelated process, never the owner.
    auto *late=new(late_bytes) JsSession;
    REQUIRE(!late->start(1,42)); pump(*late);
    os.hold_kill=true; late->cancel(); REQUIRE(os.kills==kills+1);
    os.now+=JS_SERVICE_REAP; late->poll();
    REQUIRE(late->state()==JsSession::State::stranded && os.kills==kills+1);
    REQUIRE(late->start(2,42)<0);
    os.child=0;
}
int main() {
    protocol(); sessions(); terminal_ownership();
    std::puts("JS_SERVICE_HOST_OK framing=1MiB fields=18 bounded_pump=8 recovery=2"); return 0;
}
