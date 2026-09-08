#include "script_fetch.hpp"
#include "browser_response.h"
#include <string.h>
namespace reist::browser {
struct ScriptFetch::Cache {
    struct Entry { char url[BROWSER_RESOURCE_URL_CAPACITY],effective[BROWSER_RESOURCE_URL_CAPACITY]; uint32_t offset,length; uint64_t expires; };
    uint32_t count,used;
    Entry entries[BROWSER_SCRIPT_COUNT];
    char bytes[BROWSER_SCRIPT_SOURCE];
};
static bool network(const char *s) { return !strncmp(s,"http://",7)||!strncmp(s,"https://",8); }
static void reaped(int pid,uint32_t generation,int status) {
    x86os_puts("BROWSER_SCRIPT_FETCH_REAP pid=");x86os_print_number(pid);
    x86os_puts(" generation=");x86os_print_number((int)generation);
    x86os_puts(" status=");x86os_print_number(status);x86os_puts("\n");
}
static bool copy_url(char *out,const char *s) {
    if(!s)return false;
    uint32_t n=0;while(n<BROWSER_RESOURCE_URL_CAPACITY&&s[n])++n;
    if(n==BROWSER_RESOURCE_URL_CAPACITY)return false;memcpy(out,s,n+1);return true;
}
static bool utf8(const uint8_t *s,uint32_t n) {
    uint32_t left=0,scalar=0,minimum=0;
    for(uint32_t i=0;i<n;++i) {
        uint32_t c=s[i];
        if(left) {
            if((c&192)!=128) return false;
            scalar=(scalar<<6)|(c&63);
            if(!--left && (scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))) return false;
        } else if(c>=128) {
            if(c>=194 && c<=223) {left=1;scalar=c&31;minimum=128;}
            else if(c>=224 && c<=239) {left=2;scalar=c&15;minimum=2048;}
            else if(c>=240 && c<=244) {left=3;scalar=c&7;minimum=65536;}
            else return false;
        }
    }
    return !left;
}
int ScriptFetch::clock() {
    uint64_t now=0;if(x86os_monotonic_ms(&now)||now<last_)return -84;last_=now;return 0;
}
int ScriptFetch::peer() {
    for(unsigned attempt=0;attempt<2;++attempt) {
        for(unsigned i=0;i<32;++i) {
            x86os_process_info_t p{};if(x86os_process_info(i,&p)<=0)break;
            if(p.pid!=pid_)continue;
            if(p.parent_pid!=x86os_getpid())return -84;
            if(p.state==X86OS_PROCESS_ZOMBIE)return p.state; // Spawned PID pinned until our wait.
            x86os_process_identity_t id{};int rc=x86os_process_identity_of(pid_,&id);
            if(rc==-3)break;
            if(rc||id.version!=1||id.struct_size!=sizeof(id)||id.pid!=pid_||!id.generation||
                (generation_&&generation_!=id.generation))return -84;
            generation_=id.generation;return p.state;
        }
    }
    return -84;
}
void ScriptFetch::fence() {
    if(endpoint_){(void)x86os_ipc_close(endpoint_);endpoint_=0;}
    if(fd_>=0){(void)x86os_close(fd_);fd_=-1;}
    result_=nullptr;length_=0;
}
void ScriptFetch::fail(int rc) {
    if(state_==6||state_==4)return;
    x86os_puts("BROWSER_SCRIPT_FETCH_FAIL phase=");x86os_print_number((int)state_);
    x86os_puts(" status=");x86os_print_number(rc);
    x86os_puts(" received=");x86os_print_number((int)used_);
    x86os_puts(" total=");x86os_print_number((int)total_);x86os_puts("\n");
    error_=rc;fence();
    if(!pid_){state_=5;return;}
    int p=peer();if(p<0){state_=6;return;}
    state_=4;reap_deadline_=last_>UINT64_MAX-1000?UINT64_MAX:last_+1000;
    if(p!=X86OS_PROCESS_ZOMBIE&&!killed_){killed_=true;(void)x86os_kill(pid_);}
}
void ScriptFetch::cancel(){if(state_!=0&&state_!=5&&state_!=6)fail(-125);}
void ScriptFetch::reset_cache(){if(cache_)cache_->count=cache_->used=0;result_=nullptr;length_=0;}
int ScriptFetch::release() {
    if(busy()||pid_||endpoint_||fd_>=0||stranded())return -84;
    x86os_free(cache_);x86os_free(buffer_);cache_=nullptr;buffer_=nullptr;result_=nullptr;state_=0;return 0;
}
int ScriptFetch::hop() {
    if(pid_||endpoint_||fd_>=0)return -84;
    used_=total_=0;killed_=false;generation_=0;
    if(x86os_ipc_create(&endpoint_))return -5;
    char arg[11],reverse[10];uint32_t n=0,v=endpoint_;
    do{reverse[n++]=(char)('0'+v%10);v/=10;}while(v);
    for(uint32_t i=0;i<n;++i)arg[i]=reverse[n-i-1];arg[n]=0;
    const char *args[]={"/usr/bin/curl.prg","--reist-ipc",arg,"--max-bytes","1048576","--include",url_};
    pid_=x86os_spawnv(args[0],7,args);
    if(pid_<=0){pid_=0;return -5;}
    state_=1;
    if(peer()<0||x86os_ipc_delegate(endpoint_,pid_,X86OS_IPC_RIGHT_SEND))return -84;
    x86os_puts("BROWSER_SCRIPT_FETCH_WORKER pid=");x86os_print_number(pid_);
    x86os_puts(" generation=");x86os_print_number((int)generation_);x86os_puts("\n");
    return 0;
}
int ScriptFetch::start(const char *document,const char *canonical) {
    if(busy()||pid_||endpoint_||fd_>=0||stranded())return -84;
    state_=0;error_=0;result_=nullptr;length_=0;skipped_=false;redirects_=used_=total_=0;
    alias_deadline_=UINT64_MAX;
    if(clock()||last_>UINT64_MAX-5000){fail(-84);return error_;}deadline_=last_+5000;
    if(!copy_url(document_,document)||!copy_url(initial_,canonical)||!copy_url(url_,canonical))return -22;
    if(browser_resource_admit(document_,url_)){state_=3;skipped_=true;return 0;}
    if(cache_)for(uint32_t i=0;i<cache_->count;++i){const auto &c=cache_->entries[i];
        if(c.expires>last_&&(!strcmp(c.url,url_)||!strcmp(c.effective,url_))){result_=cache_->bytes+c.offset;length_=c.length;state_=3;return 0;}}
    if(!buffer_)buffer_=static_cast<uint8_t *>(x86os_malloc(BROWSER_SCRIPT_SOURCE+REIST_CURL_HEADER_CAPACITY));
    if(!buffer_){fail(-12);return error_;}
    if(network(url_)){int rc=hop();if(rc)fail(rc);return error_;}
    // Local query strings are URL metadata, not part of a VFS filename.
    char *query=strchr(url_,'?');if(query)*query=0;
    fd_=x86os_open(url_);if(fd_<0){state_=3;skipped_=true;return 0;}state_=2;return 0;
}
void ScriptFetch::complete(uint32_t offset,uint32_t length,uint32_t age) {
    if(length>BROWSER_SCRIPT_SOURCE){fail(-28);return;}
    if(!utf8(buffer_+offset,length)){state_=3;skipped_=true;return;}
    result_=reinterpret_cast<char *>(buffer_+offset);length_=length;state_=3;
    if(!age)return;
    static_assert(sizeof(Cache)+sizeof(ScriptFetch)+BROWSER_SCRIPT_SOURCE+REIST_CURL_HEADER_CAPACITY<3U*1024U*1024U);
    if(!cache_){cache_=static_cast<Cache *>(x86os_malloc(sizeof(Cache)));if(!cache_){fail(-12);return;}cache_->count=cache_->used=0;}
    if(cache_->count==BROWSER_SCRIPT_COUNT||length>BROWSER_SCRIPT_SOURCE-cache_->used){fail(-28);return;}
    auto &e=cache_->entries[cache_->count++];copy_url(e.url,alias_deadline_>last_?initial_:url_);copy_url(e.effective,url_);
    e.offset=cache_->used;e.length=length;e.expires=age==UINT32_MAX?UINT64_MAX:last_+age;
    if(alias_deadline_>last_ && e.expires>alias_deadline_)e.expires=alias_deadline_;
    if(length)memcpy(cache_->bytes+e.offset,result_,length);cache_->used+=length;result_=cache_->bytes+e.offset;
}
void ScriptFetch::finish() {
    static browser_response_t r;
    int rc=browser_response_open_kind(buffer_,total_,url_,BROWSER_RESPONSE_SCRIPT,&r);
    if(rc==1){
        if(redirects_==BROWSER_REDIRECT_LIMIT){fail(-40);return;}
        uint32_t age=browser_response_script_max_age(buffer_,r.body_offset);
        uint64_t expires=age ? last_+age : 0;
        if(expires<alias_deadline_)alias_deadline_=expires;
        static char target[BROWSER_RESOURCE_URL_CAPACITY];
        if(browser_resource_url(url_,r.redirect,target)||browser_resource_admit(document_,target)||browser_resource_admit(url_,target)){
            state_=3;skipped_=true;return;
        }
        ++redirects_;copy_url(url_,target);rc=hop();if(rc)fail(rc);return;
    }
    if(rc==-5||rc==-95||rc==-13){state_=3;skipped_=true;return;}
    if(rc){fail(rc);return;}
    complete(r.body_offset,r.body_length,browser_response_script_max_age(buffer_,r.body_offset));
}
void ScriptFetch::poll() {
    if(!busy())return;
    if(clock()){fail(-84);if(state_==4)state_=6;return;}
    if(state_==4){
        int p=peer();if(p<0){state_=6;return;}
        if(p==X86OS_PROCESS_ZOMBIE){int status=0;if(x86os_wait(pid_,&status)!=pid_){state_=6;return;}reaped(pid_,generation_,status);pid_=0;state_=5;return;}
        if(last_>=reap_deadline_)state_=6;return;
    }
    if(last_>=deadline_){fail(-110);return;}
    if(state_==2){
        uint32_t cap=BROWSER_SCRIPT_SOURCE+1-used_;if(cap>4096)cap=4096;
        int n=x86os_read(fd_,buffer_+used_,cap);
        if(n<0||uint32_t(n)>cap){fail(-5);return;}
        used_+=(uint32_t)n;++progress_;
        if(used_>BROWSER_SCRIPT_SOURCE){fail(-28);return;}
        if(!n){(void)x86os_close(fd_);fd_=-1;complete(0,used_,UINT32_MAX);}return;
    }
    int p=peer();if(p<0){fail(-84);return;}
    for(unsigned i=0;i<8&&(!total_||used_<total_);++i){
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),0,{0}};
        int rc=x86os_ipc_receive_bulk_timeout(endpoint_,&m,0);if(rc==-11||(!used_&&rc==-32))break;
        if(rc||m.version!=X86OS_IPC_BULK_MESSAGE_VERSION||m.struct_size!=sizeof(m)){fail(-84);return;}
        reist_curl_ipc_packet_t packet;memcpy(&packet,m.payload,sizeof(packet));
        if(reist_curl_ipc_accept(&packet,m.length,endpoint_,buffer_,BROWSER_SCRIPT_SOURCE+REIST_CURL_HEADER_CAPACITY,&used_,&total_)){fail(-84);return;}
        ++progress_;
    }
    p=peer();if(p<0){fail(-84);return;}
    if(p!=X86OS_PROCESS_ZOMBIE)return;
    // A dead sender can leave more than one turn's frames. Drain under the
    // same deadline before considering its successful exit a complete result.
    if(total_&&used_<total_)return;
    int status=-1;if(x86os_wait(pid_,&status)!=pid_){fail(-84);return;}
    reaped(pid_,generation_,status);pid_=0;fence();
    if(status||!total_||used_!=total_){fail(-84);return;}
    finish();
}
}
