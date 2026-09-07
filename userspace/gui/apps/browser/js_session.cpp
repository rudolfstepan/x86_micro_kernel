#include "js_session.hpp"
#include <string.h>
namespace reist::browser {
static void number(char out[11],uint32_t n) {
    char reverse[10]; unsigned count=0;
    do { reverse[count++]=char('0'+n%10); n/=10; } while(n);
    for(unsigned i=0;i<count;++i) out[i]=reverse[count-i-1]; out[count]=0;
}
JsSession::~JsSession() { if(pid_ || request_ || reply_) __builtin_trap(); }
int JsSession::clock(uint64_t &now) {
    if(x86os_monotonic_ms(&now) || now<last_) return -84;
    last_=now; return 0;
}
int JsSession::peer() {
    x86os_process_identity_t identity{};
    int rc=x86os_process_identity_of(pid_,&identity);
    if(rc && rc!=-3) return -84;
    if(!rc && (identity.version!=1 || identity.struct_size!=sizeof(identity) ||
        identity.pid!=pid_ || identity.generation!=header_.child_generation || !identity.generation)) return -84;
    for(unsigned i=0;i<32;++i) {
        x86os_process_info_t info{}; int found=x86os_process_info(i,&info);
        if(found<0) return -84; if(!found) break;
        if(info.pid==pid_) {
            if(info.parent_pid!=int(header_.parent_pid) || (rc && info.state!=X86OS_PROCESS_ZOMBIE)) return -84;
            return info.state;
        }
    }
    return -84;
}
void JsSession::fence() {
    if(request_) { (void)x86os_ipc_close(request_); request_=0; }
    if(reply_) { (void)x86os_ipc_close(reply_); reply_=0; }
    // Only incomplete staging is still borrowed for writes. Once published,
    // the caller may have consumed/freed its buffer; invalidate the view only.
    if((phase_==State::sending || phase_==State::receiving) && output_ && capacity_) output_[0]=0;
    input_=nullptr; output_=nullptr; input_size_=capacity_=0;
}
void JsSession::fail(int reason) {
    if(phase_==State::stranded || phase_==State::reaping) return;
    error_=reason; fence();
    if(!pid_) { phase_=State::failed; return; }
    int state=peer();
    if(state<0) { phase_=State::stranded; return; }
    phase_=State::reaping;
    reap_deadline_=last_>UINT64_MAX-JS_SERVICE_REAP ? UINT64_MAX : last_+JS_SERVICE_REAP;
    if(state!=X86OS_PROCESS_ZOMBIE && !killed_) {
        killed_=true;
        // Exit can race kill and publish a zombie later. Never repeat kill or
        // forget the pinned child; the same bounded reap path handles both.
        (void)x86os_kill(pid_);
    }
}
void JsSession::reap() {
    int state=peer();
    if(state<0) { error_=-84; phase_=State::stranded; return; }
    if(state==X86OS_PROCESS_ZOMBIE) {
        int status=-1;
        if(x86os_wait(pid_,&status)!=pid_) { error_=-84; phase_=State::stranded; return; }
        pid_=0; exit_status_=status; fence();
        if(!error_ && status) error_=-84;
        phase_=error_ ? State::failed : State::closed; return;
    }
    uint64_t now=0;
    if(clock(now) || now>=reap_deadline_) { error_=-110; phase_=State::stranded; }
}
int JsSession::begin(uint32_t op,const void *data,uint32_t size,void *output,uint32_t cap,uint32_t budget) {
    if(!budget || budget>JS_SERVICE_DEADLINE || header_.sequence==UINT32_MAX) return -22;
    uint64_t now=0; if(clock(now) || now>UINT64_MAX-budget) { fail(-84); return -84; }
    header_.operation=op; ++header_.sequence; header_.deadline=now+budget;
    input_=static_cast<const char *>(data); input_size_=size;
    output_=static_cast<char *>(output); capacity_=cap;
    if(output_ && cap) output_[0]=0;
    sent_=0; sent_empty_=false; receive_={0,UINT32_MAX,UINT32_MAX};
    engine_status_=0; phase_=State::sending; return 0;
}
int JsSession::start(uint32_t document,uint64_t seed,uint32_t probe) {
    if(pid_ || request_ || reply_ || busy() || phase_==State::stranded || !document || !seed || probe>3) return -22;
    if(document<document_ || (document==document_ && phase_!=State::failed)) return -22;
    if(document==document_) { if(recoveries_==2) return -28; ++recoveries_; }
    else recoveries_=0;
    document_=document; error_=0; exit_status_=-1; killed_=false;
    seed_=seed; uint64_t now=0;
    if(clock(now)) { error_=-84; phase_=State::failed; return -84; }
    x86os_process_identity_t parent{};
    if(x86os_process_identity_of(x86os_getpid(),&parent) || parent.version!=1 ||
        parent.struct_size!=sizeof(parent) || parent.pid!=x86os_getpid() || !parent.generation) { fail(-84); return -84; }
    header_={}; header_.magic=JS_SERVICE_MAGIC; header_.version=JS_SERVICE_VERSION; header_.size=JS_SERVICE_HEADER;
    header_.parent_pid=uint32_t(parent.pid); header_.parent_generation=parent.generation; header_.document=document;
    if(x86os_ipc_create(&request_) || x86os_ipc_create(&reply_)) { fail(-5); return -5; }
    char a[11],b[11],c[11],d[11],e[11];
    number(a,request_); number(b,reply_); number(c,header_.parent_pid); number(d,parent.generation); number(e,probe);
    const char *args[]={"/usr/bin/jswork.prg",a,b,c,d,e};
    pid_=x86os_spawnv(args[0],6,args);
    if(pid_<=0) { pid_=0; fail(-5); return -5; }
    header_.child_pid=uint32_t(pid_);
    x86os_process_identity_t child{};
    if(x86os_process_identity_of(pid_,&child) || child.version!=1 || child.struct_size!=sizeof(child) ||
        child.pid!=pid_ || !child.generation) { fail(-84); return -84; }
    header_.child_generation=child.generation;
    if(peer()<0 || x86os_ipc_delegate(request_,pid_,X86OS_IPC_RIGHT_RECEIVE) ||
        x86os_ipc_delegate(reply_,pid_,X86OS_IPC_RIGHT_SEND)) { fail(-84); return -84; }
    return begin(JS_OP_HELLO,&seed_,sizeof(seed_),internal_,sizeof(internal_),JS_SERVICE_DEADLINE);
}
int JsSession::evaluate(const char *data,uint32_t size,char *out,uint32_t cap,uint32_t budget) {
    if(!ready() || !data || size>JS_SERVICE_SOURCE || !out || !cap || cap>JS_SERVICE_RESULT) return -22;
    return begin(JS_OP_EVAL,data,size,out,cap,budget);
}
int JsSession::health(bool collect) {
    if(!ready()) return -22;
    return begin(collect?JS_OP_GC:JS_OP_HEALTH,nullptr,0,internal_,sizeof(internal_),JS_SERVICE_DEADLINE);
}
int JsSession::shutdown() {
    if(!ready()) return -22;
    return begin(JS_OP_SHUTDOWN,nullptr,0,nullptr,0,JS_SERVICE_DEADLINE);
}
void JsSession::cancel() {
    if(phase_!=State::closed && phase_!=State::failed && phase_!=State::stranded) fail(-125);
}
const char *JsSession::result() const {
    return ready() && header_.operation==JS_OP_EVAL && !engine_status_ ? output_ : nullptr;
}
const uint32_t *JsSession::stats() const {
    return ready() && (header_.operation==JS_OP_HEALTH || header_.operation==JS_OP_GC) && !engine_status_ ? internal_ : nullptr;
}
void JsSession::poll() {
    if(phase_==State::reaping) { reap(); return; }
    if(phase_==State::closed || phase_==State::failed || phase_==State::stranded) return;
    uint64_t now=0;
    if(clock(now)) { fail(-84); return; }
    int state=peer(); if(state<0) { fail(-84); return; }
    if(ready()) { if(state==X86OS_PROCESS_ZOMBIE) fail(-84); return; }
    if(now>=header_.deadline) { fail(-110); return; }
    for(unsigned i=0;i<8;++i) {
        x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),0,{0}};
        if(phase_==State::sending) {
            js_service_packet packet;
            js_service_packet_make(&packet,&header_,JS_SERVICE_REQUEST,input_,input_size_,sent_);
            message.length=JS_SERVICE_HEADER+packet.header.length;
            memcpy(message.payload,&packet,message.length);
            int rc=x86os_ipc_send_bulk_timeout(request_,&message,0);
            if(rc==-11) return; if(rc) { fail(rc); return; }
            sent_+=packet.header.length; sent_empty_=true; ++progress_;
            if(sent_==input_size_ && sent_empty_) phase_=State::receiving;
        } else {
            int rc=x86os_ipc_receive_bulk_timeout(reply_,&message,0);
            if(rc==-11) { if(state==X86OS_PROCESS_ZOMBIE) fail(-84); return; }
            if(rc || message.version!=X86OS_IPC_BULK_MESSAGE_VERSION || message.struct_size!=sizeof(message)) { fail(-84); return; }
            js_service_packet packet; memcpy(&packet,message.payload,sizeof(packet));
            uint32_t cap=header_.operation==JS_OP_EVAL ? capacity_-1 : capacity_;
            if(js_service_accept(&packet,message.length,&header_,1,output_,cap,&receive_)) { fail(-84); return; }
            ++progress_;
            if(receive_.offset!=receive_.total) continue;
            engine_status_=receive_.status;
            if(clock(now) || now>=header_.deadline) { fail(-110); return; }
            if(engine_status_>1 || (engine_status_ && header_.operation!=JS_OP_EVAL)) { fail(-125); return; }
            if(header_.operation==JS_OP_SHUTDOWN) {
                fence(); phase_=State::reaping; reap_deadline_=now+JS_SERVICE_REAP; return;
            }
            if(header_.operation==JS_OP_HELLO && (internal_[0]!=1 || internal_[1]!=JS_SERVICE_HEAP || internal_[2]!=16384 || internal_[3])) { fail(-84); return; }
            if((header_.operation==JS_OP_GC || header_.operation==JS_OP_HEALTH) &&
                (internal_[0]!=1 || internal_[1]!=24 || internal_[2]>internal_[3] || internal_[3]>JS_SERVICE_HEAP ||
                 internal_[4]<internal_[3] || internal_[4]>JS_SERVICE_HEAP || internal_[5])) { fail(-84); return; }
            int final_state=peer();
            if(final_state<0 || final_state==X86OS_PROCESS_ZOMBIE) { fail(-84); return; }
            if(header_.operation==JS_OP_EVAL && output_) output_[receive_.total]=0;
            phase_=State::idle; input_=nullptr; input_size_=0; return;
        }
    }
}
}
