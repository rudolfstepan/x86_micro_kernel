#include "file_broker.hpp"
extern "C" {
#include "reist/vfs_file_client.h"
}
#include <stdlib.h>
#include <string.h>
namespace reist::script {
static uint32_t next_lease=0; // Process-scoped, never wrap or reuse.
FileBroker::~FileBroker(){if(response_ || manifest_.count) __builtin_trap();}
int FileBroker::remaining(uint64_t deadline,uint32_t &ms) {
    uint64_t now=0;
    if(x86os_monotonic_ms(&now) || now<last_ || now>=deadline) return -110;
    last_=now; ms=deadline-now>1000?1000:(uint32_t)(deadline-now); return 0;
}
int FileBroker::admit(const char paths[4][192],uint32_t count) {
    if(response_ || manifest_.count || uncertain_ || !paths || !count || count>4 || next_lease==UINT32_MAX) return -22;
    calls_=bytes_=0;
    // Entire grant set validated before allocation/open; never accept traversal.
    for(uint32_t i=0;i<count;++i) {
        const char *end=(const char *)memchr(paths[i],0,192);
        if(!end || end==paths[i]) return -22;
        for(const char *p=paths[i];p<end;) {
            if(*p=='/' || *p=='\\') {++p;continue;}
            const char *start=p; while(p<end && *p!='/' && *p!='\\') ++p;
            if(p-start==2 && start[0]=='.' && start[1]=='.') return -13;
        }
    }
    uint64_t now=0;
    if(x86os_monotonic_ms(&now) || now>UINT64_MAX-5000) return -110;
    last_=now; uint64_t deadline=now+5000;
    response_=(char *)malloc(JS_FILE_RESPONSE); if(!response_)return -12;
    manifest_={1,sizeof(manifest_),count,0,{}}; ++next_lease;
    int error=0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t ms=0;
        error=remaining(deadline,ms);
        if(!error) error=reist_vfs_file_open_flags(paths[i],ms,REIST_VFS_FILE_RIGHT_DATA,X86OS_O_NOFOLLOW,&handles_[i]);
        if(error || !handles_[i]) {if(!error)error=-84; break;}
        manifest_.files[i]={i+1,next_lease,JS_FILE_RIGHTS,0};
    }
    uint32_t ms=0; if(!error)error=remaining(deadline,ms);
    if(error) {int cleanup=close(); return cleanup?cleanup:error;}
    return 0;
}
int FileBroker::serve(const void *input,uint32_t size,uint64_t deadline) {
    js_file_request q;
    if(!response_ || uncertain_ || js_file_request_valid(input,size,&q) || q.call!=calls_+1) return -84;
    uint32_t ms=0; if(remaining(deadline,ms)) return -110;
    if(calls_>=JS_FILE_CALLS) return -28;
    ++calls_;
    js_file_reply r={1,sizeof(r),q.call,0,0,0,{0,0}};
    uint32_t index=q.slot-1;
    if(!q.slot || index>=manifest_.count || q.lease!=manifest_.files[index].lease) r.error=-13;
    else if(q.operation<JS_FILE_READ || q.operation>JS_FILE_CLOSE) r.error=-13;
    else if((q.operation==JS_FILE_READ && (!q.argument || q.argument>JS_FILE_CHUNK)) ||
            ((q.operation==JS_FILE_SIZE || q.operation==JS_FILE_CLOSE) && q.argument)) r.error=-22;
    else if(!handles_[index]) r.error=q.operation==JS_FILE_CLOSE?0:-9;
    else {
        uint32_t handle=handles_[index];
        if(reist_vfs_file_set_timeout(handle,ms)) return -84;
        if(q.operation==JS_FILE_READ) {
            if(q.argument>JS_FILE_BYTES-bytes_) return -28;
            int n=reist_vfs_file_read_bulk(handle,response_+sizeof(r),q.argument);
            if(n<0)r.error=n;
            else if((uint32_t)n>q.argument)return -84;
            else {r.bytes=(uint32_t)n; bytes_+=(uint32_t)n;}
        } else if(q.operation==JS_FILE_SEEK) {
            r.error=reist_vfs_file_seek(handle,q.argument,REIST_VFS_SEEK_SET,&r.value);
        } else if(q.operation==JS_FILE_SIZE) {
            x86os_file_info_t info{}; r.error=reist_vfs_file_fstat(handle,&info);
            if(!r.error && info.type!=X86OS_FILE)r.error=-84;
            if(!r.error)r.value=info.size;
        } else {
            r.error=reist_vfs_file_close(handle); handles_[index]=0;
            if(r.error) {uncertain_=true; return -84;}
        }
    }
    if(remaining(deadline,ms)) return -110;
    if(r.error)r.bytes=r.value=0;
    memcpy(response_,&r,sizeof(r)); response_size_=sizeof(r)+r.bytes;
    return js_file_reply_valid(response_,response_size_,&q);
}
int FileBroker::close() {
    if(!response_ && !manifest_.count)return uncertain_?-84:0;
    int error=uncertain_?-84:0; uint64_t now=0;
    if(x86os_monotonic_ms(&now) || now<last_ || now>UINT64_MAX-5000)error=-110;
    uint64_t deadline=now>UINT64_MAX-5000?UINT64_MAX:now+5000;
    for(unsigned i=0;i<4;++i)if(handles_[i]) {
        uint32_t ms=0;
        if(remaining(deadline,ms)) {error=-110;handles_[i]=0;continue;}
        if(reist_vfs_file_set_timeout(handles_[i],ms)) error=-84;
        if(reist_vfs_file_close(handles_[i]))error=-84;
        handles_[i]=0;
    }
    uncertain_=error!=0; manifest_={}; free(response_);response_=nullptr;response_size_=0;
    return error;
}
}
