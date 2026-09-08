#include "file_broker.hpp"
#include "../userspace/gui/apps/browser/js_session.hpp"
#define main retained_service_main
#include "js_service_host.cpp"
#undef main
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if(!(x)){std::printf("JS_FILES_HOST_FAIL line=%d\n",__LINE__);std::exit(1);} } while(0)
extern "C" {
#include "reist/vfs_file_client.h"
}
static uint64_t &now=os.now;
static unsigned opens,reads,closes,seeks,stats,live,io_error,close_error;
static bool full_read=false;static uint32_t delay=0;
static uint32_t positions[5];
int reist_vfs_file_open_flags(const char *path,uint32_t ms,uint32_t rights,uint32_t flags,uint32_t *out){
    CHECK(ms && ms<=1000 && rights==REIST_VFS_FILE_RIGHT_DATA && flags==X86OS_O_NOFOLLOW);
    ++opens; if(!strcmp(path,"bad"))return -40;
    CHECK(live<4); *out=++live;positions[*out]=0;return 0;
}
int reist_vfs_file_set_timeout(uint32_t h,uint32_t ms){CHECK(h && h<=4 && ms && ms<=1000);return 0;}
int reist_vfs_file_read_bulk(uint32_t h,void *data,size_t n){
    ++reads; CHECK(h && h<=4 && n && n<=131072);now+=delay;if(io_error)return -(int)io_error;
    if(full_read){memset(data,'q',n);return (int)n;}
    unsigned count=positions[h]<6?6-positions[h]:0;if(count>n)count=(unsigned)n;
    if(count)memcpy(data,&"abcdef"[positions[h]],count);positions[h]+=count;return (int)count;
}
int reist_vfs_file_seek(uint32_t h,int64_t offset,uint32_t whence,uint32_t *out){
    ++seeks;CHECK(h && h<=4 && offset>=0 && offset<=UINT32_MAX && whence==0);
    *out=positions[h]=(uint32_t)offset;return 0;
}
int reist_vfs_file_fstat(uint32_t h,x86os_file_info_t *out){++stats;CHECK(h && h<=4);*out={};out->type=X86OS_FILE;out->size=6;return 0;}
int reist_vfs_file_close(uint32_t h){CHECK(h && h<=4);++closes;return -(int)close_error;}
using namespace reist::script;
static void broker_cases(){
    FileBroker broker;char paths[4][192]={{"one"},{"two"}};
    CHECK(!broker.admit(paths,2)); CHECK(opens==2);
    const js_file_manifest manifest=broker.manifest();
    js_file_request q={1,sizeof(q),1,manifest.files[0].slot,manifest.files[0].lease,JS_FILE_READ,3,0};
    CHECK(!broker.serve(&q,sizeof(q),5000));
    auto r=(const js_file_reply *)broker.data(); CHECK(!r->error && r->bytes==3);
    CHECK(!memcmp((const char *)broker.data()+sizeof(*r),"abc",3));
    ++q.call;q.lease++; CHECK(!broker.serve(&q,sizeof(q),5000));
    CHECK(((const js_file_reply *)broker.data())->error==-13 && reads==1);
    q.lease--;q.call++;q.operation=JS_FILE_CLOSE;q.argument=0;
    CHECK(!broker.serve(&q,sizeof(q),5000));++q.call;CHECK(!broker.serve(&q,sizeof(q),5000));CHECK(closes==1);
    ++q.call;q.operation=JS_FILE_READ;q.argument=1;
    CHECK(!broker.serve(&q,sizeof(q),5000));CHECK(((const js_file_reply *)broker.data())->error==-9 && reads==1);
    // Malformed fields never consume a call or touch the backend.
    q.call++;q.slot=manifest.files[1].slot;
    for(unsigned field: {0U,1U,2U,7U}) {
        js_file_request bad=q;((uint32_t *)&bad)[field]=UINT32_MAX;
        CHECK(broker.serve(&bad,sizeof(bad),5000)<0 && reads==1 && seeks==0 && stats==0);
    }
    for(unsigned n=0;n<32;++n)CHECK(broker.serve(&q,n,5000)<0);
    q.operation=JS_FILE_SIZE;q.argument=0;CHECK(!broker.serve(&q,32,5000));
    CHECK(((const js_file_reply *)broker.data())->value==6 && stats==1);
    ++q.call;q.operation=JS_FILE_SEEK;q.argument=2;CHECK(!broker.serve(&q,32,5000));CHECK(seeks==1);
    ++q.call;q.operation=JS_FILE_READ;q.argument=128*1024;CHECK(!broker.serve(&q,32,5000));
    CHECK(((const js_file_reply *)broker.data())->bytes==4 && !memcmp((char *)broker.data()+32,"cdef",4));
    ++q.call;CHECK(!broker.serve(&q,32,5000));CHECK(!((const js_file_reply *)broker.data())->bytes);
    unsigned old_reads=reads;
    ++q.call;q.operation=99;CHECK(!broker.serve(&q,32,5000));CHECK(((const js_file_reply *)broker.data())->error==-13 && reads==old_reads);
    ++q.call;q.operation=JS_FILE_READ;q.argument=UINT32_MAX;CHECK(!broker.serve(&q,32,5000));
    CHECK(((const js_file_reply *)broker.data())->error==-22 && reads==old_reads);
    ++q.call;q.argument=1;io_error=116;CHECK(!broker.serve(&q,32,5000));
    CHECK(((const js_file_reply *)broker.data())->error==-116 && opens==2);io_error=0;
    CHECK(broker.serve(&q,32,5000)==-84); // exact replay denied
    CHECK(!broker.close());CHECK(closes==2);CHECK(!broker.close());live=0;
    // Full-set path validation happens before the first open.
    strcpy(paths[1],"../outside");CHECK(broker.admit(paths,2)<0 && opens==2);
    strcpy(paths[1],"bad");CHECK(broker.admit(paths,2)<0 && opens==4 && closes==3);live=0;
    strcpy(paths[1],"two");CHECK(!broker.admit(paths,2));
    CHECK(broker.manifest().files[0].lease!=manifest.files[0].lease);
    q={1,32,1,1,manifest.files[0].lease,JS_FILE_READ,3,0};old_reads=reads;
    CHECK(!broker.serve(&q,32,5000));CHECK(((const js_file_reply *)broker.data())->error==-13 && reads==old_reads);
    q.lease=broker.manifest().files[0].lease;q.call++;q.argument=JS_FILE_CHUNK;full_read=true;
    for(unsigned i=0;i<JS_FILE_BYTES/JS_FILE_CHUNK;++i){CHECK(!broker.serve(&q,32,5000));++q.call;}
    CHECK(broker.serve(&q,32,5000)==-28);full_read=false;
    CHECK(!broker.close());live=0;CHECK(!broker.admit(paths,1));
    q={1,32,1,1,broker.manifest().files[0].lease,JS_FILE_SIZE,0,0};
    for(unsigned i=0;i<JS_FILE_CALLS;++i){CHECK(!broker.serve(&q,32,5000));++q.call;}
    CHECK(broker.serve(&q,32,5000)==-28);
    CHECK(!broker.close());live=0;CHECK(!broker.admit(paths,1));
    q={1,32,1,1,broker.manifest().files[0].lease,JS_FILE_READ,3,0};
    delay=1000;CHECK(broker.serve(&q,32,now+1)==-110);delay=0;CHECK(!broker.close());live=0;
    CHECK(!broker.admit(paths,1));close_error=5;CHECK(broker.close()<0 && broker.uncertain());
    CHECK(broker.admit(paths,1)<0);close_error=0;live=0;
}
static void replies(){
    js_file_request q={1,32,1,1,1,JS_FILE_READ,3,0};
    char packet[35]{};js_file_reply r={1,32,1,0,3,0,{0,0}};memcpy(packet,&r,32);memcpy(packet+32,"abc",3);
    CHECK(!js_file_reply_valid(packet,35,&q));
    for(unsigned field=0;field<8;++field){char bad[35];memcpy(bad,packet,35);uint32_t word=UINT32_MAX;memcpy(bad+field*4,&word,4);CHECK(js_file_reply_valid(bad,35,&q)<0);}
    for(unsigned n=0;n<35;++n)CHECK(js_file_reply_valid(packet,n,&q)<0);
    r.error=-13;memcpy(packet,&r,32);CHECK(js_file_reply_valid(packet,35,&q)<0);
    r.bytes=0;memcpy(packet,&r,32);CHECK(!js_file_reply_valid(packet,32,&q));
    char source[107]{};
    js_file_manifest manifest={1,80,1,0,{{1,9,7,0}}};
    js_script_request script={1,24,1,2,1,0};
    memcpy(source,&manifest,80);memcpy(source+80,&script,24);memcpy(source+104,"f\0x",3);
    js_file_manifest output;js_script_source decoded;
    CHECK(!js_file_source_decode(source,sizeof(source),&output,&decoded) && decoded.source_bytes==1);
    for(unsigned word=0;word<20;++word){
        // A nonzero lease is valid manifest data; only the bound broker can
        // reject a different live lease. Zero is structurally invalid here.
        char bad[107];memcpy(bad,source,sizeof(bad));uint32_t value=word==5?0:UINT32_MAX;memcpy(bad+4*word,&value,4);
        memset(&output,0x5a,sizeof(output));auto before=output;
        CHECK(js_file_source_decode(bad,sizeof(bad),&output,&decoded)<0 && !memcmp(&output,&before,sizeof(output)));
    }
    for(unsigned n=0;n<sizeof(source);++n)CHECK(js_file_source_decode(source,n,&output,&decoded)<0);
}
// Compile the real worker bridge with explicit fake IPC/time seams; no second implementation.
#include "../userspace/js/file_worker.h"
static FileBroker *bridge_broker;
static uint32_t bridge_offset,bridge_bad;
static js_service_header bridge_header;
static int bridge_send(uint32_t,const x86os_ipc_bulk_message_t *message,uint32_t ms){
    CHECK(ms && ms<=5000);js_service_packet p;memcpy(&p,message->payload,sizeof(p));
    js_service_receive received{0,UINT32_MAX,UINT32_MAX};js_file_request q;
    CHECK(!js_service_accept(&p,message->length,&bridge_header,0,&q,32,&received));
    bridge_offset=0;return bridge_broker->serve(&q,32,bridge_header.deadline);
}
static int bridge_receive(uint32_t,x86os_ipc_bulk_message_t *message,uint32_t ms){
    CHECK(ms && ms<=5000);js_service_packet p;
    js_service_packet_make(&p,&bridge_header,0,bridge_broker->data(),bridge_broker->size(),bridge_offset);
    bridge_offset+=p.header.length;
    if(bridge_bad==1)++p.header.child_generation;
    if(bridge_bad==2 && p.header.offset)++p.header.offset;
    if(bridge_bad==3 && !p.header.offset)p.bytes[8]^=1; // inner call mismatch
    if(bridge_bad==4)now=bridge_header.deadline;
    message->version=X86OS_IPC_BULK_MESSAGE_VERSION;message->struct_size=sizeof(*message);message->length=72+p.header.length;
    memcpy(message->payload,&p,message->length);return 0;
}
#define x86os_ipc_send_bulk_timeout bridge_send
#define x86os_ipc_receive_bulk_timeout bridge_receive
#include "../userspace/js/file_worker.c"
#undef x86os_ipc_send_bulk_timeout
#undef x86os_ipc_receive_bulk_timeout
static void worker_bridge(){
    static char staging[JS_FILE_RESPONSE],bytes[JS_FILE_CHUNK];
    char paths[4][192]={{"one"}};
    for(unsigned fault=0;fault<5;++fault){
        FileBroker broker;live=0;CHECK(!broker.admit(paths,1));bridge_broker=&broker;bridge_bad=fault;full_read=true;
        js_file_bridge b{};b.incoming=1;b.outgoing=2;b.staging=staging;b.last=now;
        b.header={};b.header.magic=JS_SERVICE_MAGIC;b.header.version=1;b.header.size=72;b.header.operation=JS_OP_FILE;
        b.header.parent_pid=7;b.header.parent_generation=3;b.header.child_pid=9;b.header.child_generation=1;
        b.header.document=1;b.header.sequence=2;b.header.deadline=now+5000;bridge_header=b.header;
        uint32_t length=0,value=0;memset(bytes,0x5a,sizeof(bytes));
        int rc=js_file_worker_call(&b,1,broker.manifest().files[0].lease,JS_FILE_READ,JS_FILE_CHUNK,bytes,&length,&value);
        if(fault){CHECK(rc<0 && !length && !value);for(char c:bytes)CHECK(c==0x5a);}
        else {CHECK(!rc && length==JS_FILE_CHUNK && !value);for(char c:bytes)CHECK(c=='q');}
        full_read=false;CHECK(!broker.close());
    }
    live=0;bridge_broker=nullptr;bridge_bad=0;
}
static void nested_transport(){
    os={};JsSession session;char output[64],source[104]{};
    CHECK(!session.start(1,42));pump(session);CHECK(session.ready());
    CHECK(!session.script_capabilities(source,sizeof(source),output,sizeof(output)));
    os.file_call=true;os.file_sent=false;
    int before=os.calls;session.poll();CHECK(os.calls-before<=8 && session.host_request());
    js_file_reply reply={1,32,1,0,0,0,{0,0}};
    CHECK(!session.host_reply(&reply,sizeof(reply)));pump(session);CHECK(session.script_result());
    CHECK(!session.shutdown());pump(session);CHECK(!session.pid());
    // A stale worker identity is fenced before any host call is exposed.
    CHECK(!session.start(2,42));pump(session);
    CHECK(!session.script_capabilities(source,sizeof(source),output,sizeof(output)));
    os.file_call=true;os.file_sent=false;os.stale=true;session.poll();os.stale=false;pump(session);
    CHECK(session.error() && session.exit_status()==143 && !session.pid());
    os.file_call=false;CHECK(!session.start(3,42));pump(session);
    CHECK(!session.evaluate("42",2,output,sizeof(output)));os.file_call=true;os.file_sent=false;
    session.poll();pump(session);CHECK(session.error() && !session.host_request() && !session.pid());
    os.file_call=false;CHECK(!session.start(4,42));pump(session);
    CHECK(!session.script_capabilities(source,sizeof(source),output,sizeof(output)));os.file_call=true;os.file_sent=false;
    session.poll();CHECK(session.host_request());unsigned prior_calls=os.calls;os.now=session.deadline();
    session.poll();CHECK(!session.host_request());pump(session);CHECK(session.error()==-110 && os.calls==prior_calls);
    os.file_call=false;
}
int main(){
    broker_cases();replies();worker_bridge();nested_transport();
    std::puts("JS_FILES_HOST_OK");return 0;
}
