#include "../userspace/gui/apps/browser/js_session.hpp"
#define main retained_service_fixture_main
#include "js_service_host.cpp"
#undef main
#include "runner.hpp"
extern "C" {
#include "reist/vfs_file_client.h"
}
#include <string>
using namespace reist::script;
static std::string file_bytes,stdout_bytes,stderr_bytes;
static uint32_t file_offset=0,read_chunk=4096;
static int opens=0,closes=0,reads=0,read_error=0,close_error=0,writes=0,write_error=0,key=0;
static uint64_t read_delay=0;
extern "C" {
int x86os_sleep_ms(uint32_t n) { os.now+=n; return 0; }
int x86os_yield() { ++os.now; return 0; }
int x86os_getchar_nonblocking() { int k=key; key=0; return k; }
void x86os_exit(int code) { std::printf("UNEXPECTED_HOST_EXIT %d\n",code); std::exit(1); }
int x86os_write(int fd,const void *data,size_t size) {
    REQUIRE(fd==1 || fd==2); ++writes;
    if(write_error) return write_error;
    size_t n=size>3?3:size; (fd==1?stdout_bytes:stderr_bytes).append((const char *)data,n); return (int)n;
}
}
int reist_vfs_file_open_rights(const char *path,uint32_t timeout,uint32_t rights,uint32_t *handle) {
    REQUIRE(!strcmp(path,"sample.js") && timeout==1000 && rights==REIST_VFS_FILE_RIGHT_READ);
    ++opens; file_offset=0; *handle=1; return 0;
}
int reist_vfs_file_set_timeout(uint32_t handle,uint32_t timeout) { REQUIRE(handle==1 && timeout && timeout<=1000); return 0; }
int reist_vfs_file_read_bulk(uint32_t handle,void *output,size_t capacity) {
    REQUIRE(handle==1 && capacity && capacity<=4096); ++reads; os.now+=read_delay;
    if(read_error) return read_error;
    size_t n=file_bytes.size()-file_offset;
    if(n>capacity) n=capacity; if(n>read_chunk) n=read_chunk;
    memcpy(output,file_bytes.data()+file_offset,n); file_offset+=(uint32_t)n; return (int)n;
}
int reist_vfs_file_close(uint32_t handle) { REQUIRE(handle==1); ++closes; return close_error; }
int reist_vfs_file_open_flags(const char *,uint32_t,uint32_t,uint32_t,uint32_t *) {REQUIRE(false);return -13;}
int reist_vfs_file_seek(uint32_t,int64_t,uint32_t,uint32_t *) {REQUIRE(false);return -13;}
int reist_vfs_file_fstat(uint32_t,x86os_file_info_t *) {REQUIRE(false);return -13;}

static void admission() {
    Source source;
    const char *args[]={"js","-e","print(scriptArgs[1])","');throw 99;//"};
    REQUIRE(!prepare(4,args,source));
    js_script_source decoded;
    REQUIRE(!js_script_decode(source.packet,source.length,&decoded));
    REQUIRE(decoded.argc==2 && !strcmp(decoded.argv[0],"<eval>") && !strcmp(decoded.argv[1],args[3]));
    REQUIRE(decoded.source_bytes==strlen(args[2]) && !memcmp(decoded.source,args[2],decoded.source_bytes));
    std::string saved(source.packet,source.length);
    for(unsigned word=0;word<6;++word) {
        uint32_t invalid=UINT32_MAX; memcpy(source.packet+word*4,&invalid,4);
        js_script_source untouched; memset(&untouched,0x5a,sizeof(untouched));
        auto before=untouched;
        REQUIRE(js_script_decode(source.packet,source.length,&untouched)==-84);
        REQUIRE(!memcmp(&before,&untouched,sizeof(before)));
        memcpy(source.packet,saved.data(),saved.size());
    }
    for(uint32_t size=0;size<source.length;++size) REQUIRE(js_script_decode(source.packet,size,&decoded)==-84);
    // Extra argv NUL/trailing bytes, embedded source NUL, no implicit eval of argv.
    source.packet[source.length-1]=0; REQUIRE(js_script_decode(source.packet,source.length,&decoded)==-84);
    release(source); release(source);
    REQUIRE(prepare(1,args,source)==64 && !source.packet);
    REQUIRE(prepare(2,args,source)==64);
    const char *bad[]={"js","--system","sample.js"}; REQUIRE(prepare(3,bad,source)==64 && !opens);
    const char *grant[]={"js","--read","data.txt","-e","1","--read"};
    REQUIRE(!prepare(6,grant,source) && source.file_count==1 && !strcmp(source.files[0],"data.txt") && !opens);
    REQUIRE(!js_script_decode(source.packet,source.length,&decoded) && decoded.argc==2 && !strcmp(decoded.argv[1],"--read"));
    release(source);REQUIRE(!source.file_count);
    const char *missing_grant[]={"js","--read"};REQUIRE(prepare(2,missing_grant,source)==64 && !opens);
    const char *traversal[]={"js","--read","a/../outside","-e","1"};REQUIRE(prepare(5,traversal,source)==64 && !opens);
    const char *backslash[]={"js","--read","a\\..\\outside","-e","1"};REQUIRE(prepare(5,backslash,source)==64 && !opens);
    const char *too_many[]={"js","--read","a","--read","b","--read","c","--read","d","--read","e","-e","1"};
    REQUIRE(prepare(13,too_many,source)==64 && !opens);
    std::string big(4096,'a'); const char *large[]={"js","-e","1",big.c_str()};
    REQUIRE(prepare(4,large,source)==64);
    const char *file[]={"js","sample.js","one"};
    file_bytes="print(42)"; read_chunk=2;
    REQUIRE(!prepare(3,file,source) && opens==1 && closes==1 && reads==6);
    REQUIRE(!js_script_decode(source.packet,source.length,&decoded) && !strcmp(decoded.argv[0],"sample.js"));
    release(source); read_chunk=4096;
    file_bytes.assign(JS_SERVICE_SOURCE,' ');
    REQUIRE(!prepare(3,file,source)); release(source);
    file_bytes+=' '; REQUIRE(prepare(3,file,source)==71 && opens==closes && !source.packet);
    file_bytes=std::string("a\0b",3); REQUIRE(prepare(3,file,source)==66 && opens==closes);
    file_bytes.clear(); REQUIRE(!prepare(3,file,source)); release(source);
    read_error=-5; REQUIRE(prepare(3,file,source)==66 && opens==closes); read_error=0;
    close_error=-5; REQUIRE(prepare(3,file,source)==66 && opens==closes); close_error=0;
    file_bytes="1"; read_delay=5001; REQUIRE(prepare(3,file,source)==124 && opens==closes); read_delay=0;
}
static void publication() {
    char packet[64]{}; js_script_reply h={1,24,0,7,2,24}; memcpy(packet,&h,24);
    uint32_t rec[]={1,4,2,4};
    memcpy(packet+24,rec,8); memcpy(packet+32,"a\x1b\t\n",4);
    memcpy(packet+36,rec+2,8); memcpy(packet+44,"err\n",4);
    REQUIRE(!js_script_reply_valid(packet,48));
    char saved[64]; memcpy(saved,packet,64);
    for(unsigned word=0;word<6;++word) {
        uint32_t invalid=UINT32_MAX; memcpy(packet+word*4,&invalid,4);
        REQUIRE(publish(packet,48)==70 && !writes); memcpy(packet,saved,64);
    }
    // Hostile last record must not publish the earlier valid stdout record.
    packet[36]=3; REQUIRE(publish(packet,48)==70 && !writes); memcpy(packet,saved,64);
    packet[47]='x'; REQUIRE(publish(packet,48)==70 && !writes); memcpy(packet,saved,64);
    for(unsigned size=0;size<48;++size) REQUIRE(publish(packet,size)==70 && !writes);
    REQUIRE(publish(packet,48)==7 && stdout_bytes=="a?\t\n" && stderr_bytes=="err\n");
    write_error=-5; REQUIRE(publish(packet,48)==74); write_error=0;
    write_error=0x10000; REQUIRE(publish(packet,48)==74); write_error=0;
}
static void lifecycle() {
    os={}; Source source; const char *args[]={"js","-e","42"};
    REQUIRE(!prepare(3,args,source));
    REQUIRE(execute(source,false)==7 && os.spawns==1 && os.waits==1 && !os.child);
    os.blocked=true; key=27;
    REQUIRE(execute(source,true)==125 && !os.child && os.kills==1);
    os.blocked=false; REQUIRE(execute(source,false)==7 && !os.child);
    release(source);
}
int main() {
    admission(); publication(); lifecycle();
    puts("JS_RUNNER_HOST_OK admission=15 hostile_headers=12 output=atomic lifecycle=3"); return 0;
}
