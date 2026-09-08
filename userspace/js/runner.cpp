/* Trusted CLI host: source admission and validated text output, no JS eval. */
#include "runner.hpp"
extern "C" {
#include "reist/vfs_file_client.h"
}
#include <stdlib.h>
#include <string.h>
namespace reist::script {
static int clock(uint64_t &last,uint64_t &now) {
    if(x86os_monotonic_ms(&now) || now<last) return -1;
    last=now; return 0;
}
static size_t bounded_length(const char *text,size_t cap) {
    if(!text) return cap;
    size_t length=0; while(length<cap && text[length]) ++length; return length;
}
void release(Source &source) { free(source.packet); source={}; }
static int read_source(const char *path,char *target,uint32_t &length) {
    reist_vfs_file_handle_t file=0;
    uint64_t last=0,now=0;
    if(clock(last,now) || now>UINT64_MAX-5000) return 70;
    uint64_t deadline=now+5000;
    if(reist_vfs_file_open_rights(path,1000,REIST_VFS_FILE_RIGHT_READ,&file)) return 66;
    int result=0; length=0;
    // Read at most limit+1 to distinguish exact-bound EOF from overlong input.
    for(unsigned iteration=0;iteration<=JS_SERVICE_SOURCE;++iteration) {
        if(clock(last,now) || now>=deadline) { result=124; break; }
        uint32_t timeout=(uint32_t)(deadline-now); if(timeout>1000) timeout=1000;
        if(reist_vfs_file_set_timeout(file,timeout)) { result=66; break; }
        uint32_t capacity=JS_SERVICE_SOURCE+1-length; if(capacity>4096) capacity=4096;
        int count=reist_vfs_file_read_bulk(file,target+length,capacity);
        if(count<0 || (uint32_t)count>capacity) { result=66; break; }
        if(!count) break;
        length+=(uint32_t)count;
        if(length>JS_SERVICE_SOURCE) { result=71; break; }
    }
    // Closing the pinned object is always attempted, including deadline paths.
    if(reist_vfs_file_close(file) && !result) result=66;
    if(!result && (clock(last,now) || now>=deadline)) result=124;
    if(!result && memchr(target,0,length)) result=66;
    return result;
}
int prepare(int argc,const char *const *argv,Source &output) {
    if(output.packet || argc<2 || argc>27 || !argv) return 64;
    int index=1; bool expression=false;
    char files[4][192]{};uint32_t file_count=0;
    while(index<argc && argv[index] && !strcmp(argv[index],"--read")) {
        if(file_count==4 || index+1>=argc || !argv[index+1])return 64;
        size_t n=bounded_length(argv[index+1],192);if(!n || n>=192)return 64;
        const char *path=argv[index+1];
        for(size_t at=0;at<n;) {
            if(path[at]=='/' || path[at]=='\\'){++at;continue;}
            size_t start=at;while(at<n && path[at]!='/' && path[at]!='\\')++at;
            if(at-start==2 && path[start]=='.' && path[start+1]=='.')return 64;
        }
        memcpy(files[file_count++],path,n+1);index+=2;
    }
    if(index>=argc)return 64;
    if(!argv[index]) return 64;
    if(!strcmp(argv[index],"-e")) { expression=true; ++index; }
    else if(!strcmp(argv[index],"--")) ++index;
    else if(argv[index][0]=='-') return 64;
    if(index>=argc || !argv[index] || !argv[index][0] || argc-index>16) return 64;
    const char *name=expression?"<eval>":argv[index];
    uint32_t args_bytes=0;
    for(int i=index;i<argc;++i) {
        size_t length=bounded_length(i==index?name:argv[i],JS_SCRIPT_ARGS);
        if(length>=JS_SCRIPT_ARGS || length+1>JS_SCRIPT_ARGS-args_bytes) return 64;
        args_bytes+=(uint32_t)length+1;
    }
    size_t inline_bytes=expression?bounded_length(argv[index],JS_SERVICE_SOURCE+1):0;
    if(inline_bytes>JS_SERVICE_SOURCE) return 71;
    char *packet=static_cast<char *>(malloc(sizeof(js_script_request)+args_bytes+
        (expression?inline_bytes:JS_SERVICE_SOURCE+1)));
    if(!packet) return 71;
    uint32_t offset=sizeof(js_script_request);
    for(int i=index;i<argc;++i) {
        const char *value=i==index?name:argv[i]; uint32_t length=(uint32_t)strlen(value)+1;
        memcpy(packet+offset,value,length); offset+=length;
    }
    uint32_t source_bytes=(uint32_t)inline_bytes;
    int result=0;
    if(expression) memcpy(packet+offset,argv[index],inline_bytes);
    else result=read_source(argv[index],packet+offset,source_bytes);
    if(result) { free(packet); return result; }
    js_script_request header={1,sizeof(header),(uint32_t)(argc-index),args_bytes,source_bytes,0};
    memcpy(packet,&header,sizeof(header));
    js_script_source checked;
    if(js_script_decode(packet,offset+source_bytes,&checked)) { free(packet); return 64; }
    output.packet=packet; output.length=offset+source_bytes;output.file_count=file_count;
    memcpy(output.files,files,sizeof(files));return 0;
}
static int write_text(int stream,const char *bytes,uint32_t length,uint64_t &last,uint64_t deadline) {
    uint32_t offset=0;
    while(offset<length) {
        uint64_t now=0; if(clock(last,now) || now>=deadline) return 74;
        char safe[256]; uint32_t amount=length-offset; if(amount>sizeof(safe)) amount=sizeof(safe);
        for(uint32_t i=0;i<amount;++i) {
            unsigned char c=(unsigned char)bytes[offset+i];
            safe[i]=((c<32 && c!='\n' && c!='\t') || c==127)?'?':(char)c;
        }
        int written=x86os_write(stream,safe,amount);
        if(written<=0 || (uint32_t)written>amount) return 74;
        offset+=(uint32_t)written;
    }
    return 0;
}
int diagnostic(int code) {
    const char *message=code==64?"js: usage: js [--read FILE]... FILE [args...] | js [--read FILE]... -e SOURCE [args...]\n":
        code==66?"js: source unavailable or invalid\n":code==71?"js: resource limit\n":
        code==74?"js: output error\n":code==124?"js: execution timeout\n":
        code==125?"js: cancelled\n":code==1?"js: script exception\n":"js: worker/protocol failure\n";
    uint64_t last=0,now=0;
    if(!clock(last,now) && now<=UINT64_MAX-1000)
        (void)write_text(2,message,(uint32_t)strlen(message),last,now+1000);
    return code;
}
int publish(const void *input,uint32_t length) {
    // No externally visible prefix before validating every record.
    if(js_script_reply_valid(input,length)) return 70;
    js_script_reply h; memcpy(&h,input,sizeof(h));
    uint64_t last=0,now=0;
    if(clock(last,now) || now>UINT64_MAX-5000) return 74;
    uint64_t deadline=now+5000;
    uint32_t offset=sizeof(h);
    for(uint32_t i=0;i<h.records;++i) {
        uint32_t record[2]; memcpy(record,(const char *)input+offset,8); offset+=8;
        int rc=write_text((int)record[0],(const char *)input+offset,record[1],last,deadline);
        if(rc) return rc; offset+=record[1];
    }
    if(h.status) diagnostic(1);
    return (int)h.exit_code;
}
int settle(JsSession &session,bool keyboard,FileBroker *broker) {
    uint64_t start=0,now=0,last=0;
    if(clock(last,start)) { session.cancel(); return 70; }
    for(unsigned i=0;i<20000 && session.busy();++i) {
        if(keyboard && x86os_getchar_nonblocking()==27) { session.cancel(); keyboard=false; }
        uint32_t progress=session.progress(); session.poll();
        if(session.host_request()) {
            int rc=broker?broker->serve(session.host_request(),32,session.deadline()):-84;
            if(rc || session.host_reply(broker->data(),broker->size())) {
                session.cancel();return rc==-28?71:rc==-110?124:70;
            }
        }
        if(clock(last,now) || now-start>6500) { session.cancel(); return 70; }
        if(session.busy() && (session.progress()==progress?x86os_sleep_ms(1):x86os_yield())) {
            session.cancel(); return 70;
        }
    }
    return session.busy() || session.state()==JsSession::State::stranded?70:0;
}
static int close(JsSession &session) {
    if(session.ready()) { if(session.shutdown()) session.cancel(); }
    else session.cancel();
    int rc=settle(session,false);
    // Lost identity must never be repaired by an unpinned kill. Exiting this
    // host lets the worker's bounded parent-loss path reclaim its own realm.
    if(session.pid()) x86os_exit(70);
    return rc;
}
int execute(const Source &source,bool keyboard) {
    js_script_source checked;
    if(js_script_decode(source.packet,source.length,&checked)) return diagnostic(64);
    if(source.file_count>4)return diagnostic(64);
    FileBroker broker;
    char *packet=nullptr;
    if(source.file_count) {
        int rc=broker.admit(source.files,source.file_count);
        if(rc) {if(broker.uncertain())x86os_exit(70);return diagnostic(rc==-12?71:66);}
        packet=(char *)malloc(source.length+80);
        if(!packet){if(broker.close())x86os_exit(70);return diagnostic(71);}
        memcpy(packet,&broker.manifest(),80);memcpy(packet+80,source.packet,source.length);
    }
    char *output=static_cast<char *>(malloc(JS_SERVICE_RESULT));
    if(!output) {free(packet);if(broker.close())x86os_exit(70);return diagnostic(71);}
    JsSession session;
    uint64_t seed=0; int code=70;
    if(!x86os_monotonic_ms(&seed)) {
        seed^=((uint64_t)(uint32_t)x86os_getpid()<<32); if(!seed) seed=1;
        int run=0;
        if(!session.start(1,seed) && !settle(session,keyboard) && session.ready() &&
           !(packet?session.script_capabilities(packet,source.length+80,output,JS_SERVICE_RESULT):
               session.script(source.packet,source.length,output,JS_SERVICE_RESULT)) &&
           !(run=settle(session,keyboard,packet?&broker:nullptr)) && session.script_result()) {
            uint32_t length=session.script_result_length();
            int closed=close(session);
            free(packet);if(broker.close()){free(output);x86os_exit(70);}
            code=closed || session.error()?70:publish(output,length);
            free(output); return code;
        }
        if(session.error()==-110 || session.engine_status()==3) code=124;
        else if(session.engine_status()==2 || session.engine_status()==4) code=71;
        else if(session.error()==-125 && !session.engine_status()) code=125;
        if(run)code=run;
    }
    (void)close(session); free(output);free(packet);if(broker.close())x86os_exit(70);return diagnostic(code);
}
}
