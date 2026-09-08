#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "script_fetch.hpp"
#include "browser_response.h"
using reist::browser::ScriptFetch;
static uint64_t now=100;
static uint32_t handle=10,endpoint,calls,spawned,killed,reaped,allocations,offset,local_offset,local_reads;
static int live=0,exit_code=0;
static bool blocked=false,malformed=false,stale=false;
static const char *response="HTTP/1.1 200 OK\r\nContent-Type: text/javascript; charset=utf-8\r\nCache-Control: max-age=60\r\nContent-Length: 3\r\n\r\n1+2";
static const char *local=nullptr,*after_redirect=nullptr;
static char large[1024*1024+16384];
extern "C" {
void *x86os_malloc(size_t n) { void *p=malloc(n);if(p)++allocations;return p; }
void x86os_free(void *p) { if(p){--allocations;free(p);} }
int x86os_getpid(){return 7;}
int x86os_monotonic_ms(uint64_t *n){*n=now;return 0;}
int x86os_open(const char *){local_offset=0;return local?5:-2;}
int x86os_read(int fd,void *out,size_t n){
    if(fd!=5 || n>4096)abort(); ++local_reads;
    size_t left=strlen(local)-local_offset;if(n>left)n=left;
    memcpy(out,local+local_offset,n);local_offset+=(uint32_t)n;return (int)n;
}
int x86os_close(int){return 0;}
void x86os_puts(const char *){}
void x86os_print_number(int){}
int x86os_process_identity_of(int pid,x86os_process_identity_t *p){
    if(pid!=7 && !live)return -3;*p={1,sizeof(*p),pid,pid==7?3U:stale?99U:4U};return 0;
}
int x86os_process_info(uint32_t index,x86os_process_info_t *p){
    if(index || !live)return 0;*p={9,7,live,0,"curl"};return 1;
}
int x86os_ipc_create(x86os_ipc_handle_t *h){*h=endpoint=++handle;return 0;}
int x86os_ipc_close(x86os_ipc_handle_t){return 0;}
int x86os_ipc_delegate(x86os_ipc_handle_t h,int pid,uint32_t rights){return h==endpoint&&pid==9&&rights==X86OS_IPC_RIGHT_SEND?0:-84;}
int x86os_spawnv(const char *path,int argc,const char *const *argv){
    if(strcmp(path,"/usr/bin/curl.prg")||argc!=7||strcmp(argv[3],"--max-bytes")||strcmp(argv[4],"1048576"))abort();
    ++spawned;offset=0;live=X86OS_PROCESS_RUNNING;return 9;
}
int x86os_kill(int pid){if(pid!=9)abort();++killed;live=X86OS_PROCESS_ZOMBIE;exit_code=143;return 0;}
int x86os_wait(int pid,int *code){if(pid!=9||live!=X86OS_PROCESS_ZOMBIE)abort();++reaped;*code=exit_code;live=0;
    if(after_redirect){response=after_redirect;after_redirect=nullptr;}return 9;}
int x86os_ipc_receive_bulk_timeout(x86os_ipc_handle_t h,x86os_ipc_bulk_message_t *m,uint32_t timeout){
    ++calls;if(timeout||h!=endpoint)abort();if(blocked)return -11;
    uint32_t size=(uint32_t)strlen(response);if(offset==size)return -11;
    reist_curl_ipc_packet_t p={REIST_CURL_IPC_MAGIC,h,offset,size,{0}};
    uint32_t n=size-offset;if(n>REIST_CURL_IPC_DATA)n=REIST_CURL_IPC_DATA;
    memcpy(p.bytes,response+offset,n);if(malformed)++p.offset;
    m->length=16+n;memcpy(m->payload,&p,m->length);offset+=n;
    if(offset==size)live=X86OS_PROCESS_ZOMBIE;return 0;
}
}
#define CHECK(x) do {if(!(x)){printf("SCRIPT_FETCH_FAIL line=%u\n",__LINE__);return 1;}}while(0)
static void pump(ScriptFetch &f){for(unsigned i=0;i<1024&&f.busy();++i){unsigned before=calls;f.poll();if(calls-before>8)abort();}}
int main(){
    {
        ScriptFetch f;
        CHECK(!f.start("https://a.test/","https://a.test/code.js"));pump(f);
        CHECK(f.ready()&&!f.skipped()&&f.length()==3&&!memcmp(f.data(),"1+2",3)&&spawned==1&&reaped==1);
        CHECK(!f.start("https://a.test/","https://a.test/code.js"));CHECK(f.ready()&&spawned==1);
        CHECK(!f.start("https://a.test/","http://a.test/code.js"));CHECK(f.skipped()&&spawned==1);
        CHECK(!f.start("https://a.test/","/secret"));CHECK(f.skipped()&&spawned==1);
        CHECK(!f.start("/htdocs/page.htm","/htdocs/missing.js"));CHECK(f.skipped());
        f.reset_cache();CHECK(!f.start("https://a.test/","https://a.test/code.js"));pump(f);CHECK(spawned==2);
        now+=21000;CHECK(!f.start("https://a.test/","https://a.test/code.js"));pump(f);CHECK(spawned==3);
        f.reset_cache();blocked=true;CHECK(!f.start("https://a.test/","https://a.test/code.js"));
        CHECK(f.busy()&&!f.data());f.cancel();CHECK(!f.data());pump(f);CHECK(!live&&killed==1&&!f.stranded());blocked=false;exit_code=0;
        CHECK(!f.start("https://a.test/","https://a.test/code.js"));blocked=true;now+=5001;pump(f);
        CHECK(f.error()==-110&&!live&&killed==2);blocked=false;exit_code=0;
        malformed=true;CHECK(!f.start("https://a.test/","https://a.test/code.js"));pump(f);
        CHECK(f.error()<0&&!f.data()&&!live);malformed=false;
        CHECK(f.release()==0);
    }
    CHECK(!allocations);
    {
        ScriptFetch f; local="// local\n1+2";
        CHECK(!f.start("/htdocs/page.htm","/htdocs/ext.js"));pump(f);
        CHECK(f.ready()&&!f.skipped()&&f.length()==strlen(local)&&local_reads==2);
        CHECK(!f.start("/htdocs/page.htm","/htdocs/ext.js"));CHECK(f.ready()&&local_reads==2);
        f.reset_cache();local="\xff";CHECK(!f.start("/htdocs/page.htm","/htdocs/ext.js"));pump(f);
        CHECK(f.skipped()&&!f.data());local=nullptr;CHECK(!f.release());
    }
    const char *good=response;
    {
        ScriptFetch f;unsigned before=spawned;
        response="HTTP/1.1 302 Found\r\nLocation: /final.js\r\nContent-Length: 0\r\n\r\n";
        after_redirect=good;exit_code=0;
        CHECK(!f.start("https://a.test/","https://a.test/redirect.js"));pump(f);
        CHECK(f.ready()&&!f.skipped()&&f.length()==3&&spawned==before+2);
        CHECK(!f.start("https://a.test/","https://a.test/final.js"));CHECK(f.ready()&&spawned==before+2);
        CHECK(!f.start("https://a.test/","https://a.test/redirect.js"));pump(f);
        CHECK(spawned==before+3); // A redirect without freshness is never cached by its alias.
        f.reset_cache();response="HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nbad";
        CHECK(!f.start("https://a.test/","https://a.test/missing.js"));pump(f);CHECK(f.skipped());
        response="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 3\r\n\r\nbad";
        CHECK(!f.start("https://a.test/","https://a.test/html.js"));pump(f);CHECK(f.skipped());
        response="HTTP/1.1 200 OK\r\nContent-Type: text/javascript\r\nContent-Length: 100\r\n\r\n1";
        CHECK(!f.start("https://a.test/","https://a.test/short.js"));pump(f);CHECK(f.error()<0&&!f.data());
        response=good;blocked=true;CHECK(!f.start("https://a.test/","https://a.test/fault.js"));
        live=X86OS_PROCESS_ZOMBIE;exit_code=142;pump(f);CHECK(f.error()<0&&!f.data()&&!live);blocked=false;exit_code=0;
        CHECK(!f.release());
    }
    {
        ScriptFetch f;
        int head=snprintf(large,sizeof(large),"HTTP/1.1 200 OK\r\nContent-Type: text/javascript\r\nContent-Length: %u\r\n\r\n",1024U*1024U);
        memset(large+head,' ',1024U*1024U);large[head+1024U*1024U]=0;response=large;
        CHECK(!f.start("https://a.test/","https://a.test/large.js"));pump(f);
        CHECK(f.ready()&&f.length()==1024U*1024U&&!f.pid());
        CHECK(!f.release());response=good;
    }
    CHECK(!allocations);
    browser_response_t r;
    CHECK(!browser_response_open_kind((const uint8_t *)response,strlen(response),"https://a.test/a",BROWSER_RESPONSE_SCRIPT,&r));
    CHECK(browser_response_script_max_age((const uint8_t *)response,r.body_offset)==20000);
    const char *bad="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 3\r\n\r\n1+2";
    CHECK(browser_response_open_kind((const uint8_t *)bad,strlen(bad),"https://a.test/a",BROWSER_RESPONSE_SCRIPT,&r)<0);
    const char *nocache="HTTP/1.1 200 OK\r\nCache-Control: max-age=60, no-store\r\n\r\n";
    CHECK(!browser_response_script_max_age((const uint8_t *)nocache,(uint32_t)strlen(nocache)));
    const char *headers[]={"Cache-Control: max-age=60, no-cache","Cache-Control: max-age=1, max-age=2",
        "Cache-Control: max-age=60\r\nVary: Accept","Cache-Control: max-age=60\r\nAge: 0",
        "Cache-Control: extension=\"a,max-age=60,b\""};
    for(const char *header:headers)
        CHECK(!browser_response_script_max_age((const uint8_t *)header,(uint32_t)strlen(header)));
    {
        ScriptFetch f;CHECK(!f.start("https://a.test/","https://a.test/stale.js"));
        unsigned before=killed;stale=true;pump(f);
        CHECK(f.stranded()&&!f.data()&&killed==before&&f.release()<0);
        // Exact identity loss is terminal: process teardown, never an unowned kill.
    }
    puts("SCRIPT_FETCH_OK");return 0;
}
