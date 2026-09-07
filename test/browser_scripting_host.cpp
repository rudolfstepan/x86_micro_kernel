#ifdef BROWSER_OWNER_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace std { using ::printf; using ::puts; using ::malloc; using ::free; using ::abort; using ::memcpy; using ::strcmp; using ::strlen; }
#include "browser_script.cpp"
static unsigned endpoint=10,live=0,kills=0,waits=0,calls=0,allocations=0;
static uint64_t ticks=100;
static js_service_header request;
static bool waiting=false,blocked=false;
static char evaluated[1024];
static char dom_reply[65536];
static uint32_t dom_size;
extern "C" {
const char browser_dom_data[1]={0},browser_dom_end[1]={0};
void *x86os_malloc(size_t n) { void *p=std::malloc(n); if(p) ++allocations; return p; }
void x86os_free(void *p) { if(p) { --allocations; std::free(p); } }
int x86os_getpid() { return 7; }
int x86os_monotonic_ms(uint64_t *n) { *n=ticks; return 0; }
void x86os_puts(const char *) {}
void x86os_print_number(int) {}
int x86os_process_identity_of(int pid,x86os_process_identity_t *p) {
    if(pid!=7 && !live) return -3;
    *p={1,sizeof(*p),pid,pid==7?3U:4U}; return 0;
}
int x86os_process_info(uint32_t index,x86os_process_info_t *p) {
    if(index || !live) return 0; *p={9,7,(int)live,0,"js"}; return 1;
}
int x86os_ipc_create(x86os_ipc_handle_t *h) { *h=++endpoint; return 0; }
int x86os_ipc_close(x86os_ipc_handle_t) { return 0; }
int x86os_ipc_delegate(x86os_ipc_handle_t,int pid,uint32_t rights) { return (pid==81 || pid==9) && (rights==1 || rights==2) ? 0:-84; }
int x86os_spawnv(const char *,int,const char *const *) { live=X86OS_PROCESS_RUNNING; return 9; }
int x86os_kill(int pid) { if(pid!=9) return -84; ++kills; live=X86OS_PROCESS_ZOMBIE; return 0; }
int x86os_wait(int pid,int *status) { if(pid!=9 || live!=X86OS_PROCESS_ZOMBIE) return -84; live=0; ++waits; *status=0; return 9; }
int x86os_ipc_send_bulk_timeout(x86os_ipc_handle_t h,const x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    ++calls; if(timeout) std::abort(); if(blocked) return -11;
    if(h==11 || h==14 || h>=17) {
        browser_css_packet_t p; std::memcpy(&p,m->payload,sizeof(p));
        if(p.magic==BROWSER_CSS_PACKET_MAGIC) { dom_size=m->length-16; std::memcpy(dom_reply,p.bytes,dom_size); return 0; }
    }
    js_service_packet p; std::memcpy(&p,m->payload,sizeof(p)); request=p.header;
    if(p.header.length>=sizeof(evaluated)) return -84;
    std::memcpy(evaluated,p.bytes,p.header.length); evaluated[p.header.length]=0; waiting=true; return 0;
}
int x86os_ipc_receive_bulk_timeout(x86os_ipc_handle_t,x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    ++calls; if(timeout) std::abort(); if(!waiting || blocked) return -11;
    uint32_t hello[]={1,JS_SERVICE_HEAP,16384,0};
    const char *patch="000000010000000141";
    const void *data=""; uint32_t size=0;
    if(request.operation==JS_OP_HELLO) { data=hello; size=sizeof(hello); }
    else if(!std::strcmp(evaluated,"__reistDOM.take()")) { data=patch; size=(uint32_t)std::strlen(patch); }
    if(request.operation==JS_OP_SHUTDOWN) { size=0; live=X86OS_PROCESS_ZOMBIE; }
    js_service_packet p; js_service_packet_make(&p,&request,0,data,size,0);
    m->length=JS_SERVICE_HEADER+p.header.length; std::memcpy(m->payload,&p,m->length); waiting=false; return 0;
}
}
#define CHECK(x) do { if(!(x)) { std::printf("BROWSER_OWNER_FAIL line=%u\n",__LINE__); return 1; } } while(0)
static void pump(browser_script_owner *s) {
    for(unsigned i=0;i<100 && (browser_script_busy(s) || s->js.busy());++i) {
        unsigned before=calls; browser_script_poll(s); if(calls-before>8) std::abort();
    }
}
static int send_script(browser_script_owner *s,const char *source) {
    browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,s->parser.request,0,0,{0}};
    browser_script_message_t h={BROWSER_SCRIPT_MAGIC,1,0,s->parser.request,7,3,81,4,1,1,(uint32_t)std::strlen(source),0};
    h.size=sizeof(h)+1+h.source_length; p.total=h.size;
    std::memcpy(p.bytes,&h,sizeof(h)); p.bytes[sizeof(h)]='0'; std::memcpy(p.bytes+sizeof(h)+1,source,h.source_length);
    return browser_script_receive(s,&p,16+p.total);
}
int main() {
    auto *s=browser_script_create(); CHECK(s && allocations==1);
    browser_html_header_t h{}; h.request=1; h.parent_pid=7; h.parent_generation=3;
    CHECK(!browser_script_prepare(s,&h,0)); CHECK(!browser_script_bind(s,81,4));
    CHECK(send_script(s,"author()")==1); pump(s); CHECK(!s->error && !s->phase && s->executions==1);
    CHECK(dom_size==sizeof(browser_script_message_t)+18 && s->candidate->count==1);
    CHECK(!browser_script_finish_parse(s)); pump(s); CHECK(!live && waits==1);
    browser_script_commit(s,0); CHECK(browser_script_has_active(s));
    ++h.request; CHECK(!browser_script_prepare(s,&h,1)); CHECK(!browser_script_bind(s,81,4));
    CHECK(send_script(s,"author()")==1); pump(s); CHECK(!live && s->executions==1);
    CHECK(!browser_script_finish_parse(s));
    ++h.request; CHECK(!browser_script_prepare(s,&h,1)); CHECK(!browser_script_bind(s,81,4));
    CHECK(send_script(s,"changed()")<0 && s->active->count==1 && !live);
    browser_script_navigation(s); ++h.request;
    CHECK(!browser_script_prepare(s,&h,0)); CHECK(!browser_script_bind(s,81,4));
    CHECK(send_script(s,"author()")==1 && live); blocked=true;
    CHECK(browser_script_destroy(s)<0); ticks+=5001; browser_script_poll(s); blocked=false; pump(s);
    CHECK(!live && kills==1 && waits==2 && s->active->count==1);
    browser_script_cancel(s); CHECK(!browser_script_destroy(s) && !allocations);
    browser_script_mutation_t items[128]; uint32_t count=99,bytes=99;
    CHECK(browser_script_journal("0000000000000003eda080",22,items,&count,&bytes)<0 && count==99 && bytes==99);
    CHECK(browser_script_journal("000000000000000141z",19,items,&count,&bytes)<0 && count==99);
    browser_script_message_t valid={BROWSER_SCRIPT_MAGIC,1,50,1,7,3,81,4,1,1,1,0};
    h.request=1;
    CHECK(!browser_script_message_valid(&valid,50,&h,81,4,1,0));
    for(unsigned field=0;field<12;++field) {
        auto bad=valid; uint32_t word; std::memcpy(&word,(char *)&bad+4*field,4); word^=UINT32_MAX;
        std::memcpy((char *)&bad+4*field,&word,4);
        CHECK(browser_script_message_valid(&bad,50,&h,81,4,1,0)<0);
    }
    for(unsigned size=0;size<50;++size) CHECK(browser_script_message_valid(&valid,size,&h,81,4,1,0)<0);
    std::puts("BROWSER_OWNER_REPLAY_CANCEL_OK"); return 0;
}
#else
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <reist_js.h>
extern "C" int *reist_libc_errno() { static int error; return &error; }
static int now(void *,uint64_t *n) { *n=(uint64_t)clock()*1000/CLOCKS_PER_SEC; return 0; }
#define CHECK(x) do { if(!(x)) { std::printf("BROWSER_BINDING_FAIL line=%u\n",__LINE__); return 1; } } while(0)
static int eval(reist_js_engine *e,const char *code,char *out) {
    uint64_t t; now(nullptr,&t); size_t size=0;
    int status=reist_js_eval(e,code,std::strlen(code),t+5000,out,65536,&size);
    if(status) {
        std::printf("eval status=%d\n",status);
        const char *probe="try { document.getElementById('target'); 'lookup-ok' } catch(e) { String(e) }";
        size_t n=0; char detail[256];
        if(!reist_js_eval(e,probe,std::strlen(probe),t+5000,detail,sizeof(detail),&n)) std::printf("lookup diagnostic=%s\n",detail);
    }
    return status;
}
int main(int argc,char **argv) {
    CHECK(argc==2); FILE *f=std::fopen(argv[1],"rb"); CHECK(f);
    static char source[65536],out[65536]; size_t length=std::fread(source,1,sizeof(source)-1,f);
    CHECK(length && !std::ferror(f) && std::feof(f)); std::fclose(f); source[length]=0;
    uint16_t control=0x037f; uint32_t simd=0x1f80;
    __asm__ volatile("fldcw %0; ldmxcsr %1" :: "m"(control),"m"(simd));
    reist_js_config cfg={1,sizeof(cfg),32*1024*1024,16384,1024*1024,65536,1024,0,42,nullptr,now};
    reist_js_status status; auto *e=reist_js_create(&cfg,&status); CHECK(e && !status);
    CHECK(!eval(e,source,out));
    CHECK(!eval(e,"__reistDOM.sync('https://example.test/',[[9,0,0,2,0,'','',''],[1,1,1,3,0,'html','',''],[1,1,2,4,6,'head','',''],[1,1,3,5,0,'title','',''],[3,0,4,0,0,'','','Old'],[1,1,2,7,0,'body','',''],[1,1,6,8,0,'p','target',''],[3,0,7,0,0,'','','Hello']]);",out));
    CHECK(!eval(e,"globalThis.saved=document.getElementById('target'); [window===globalThis,self===window,document.URL,document.title,saved.textContent,saved.tagName,document.getElementById('future')===null].join('|')",out));
    CHECK(!std::strcmp(out,"true|true|https://example.test/|Old|Hello|P|true"));
    CHECK(!eval(e,"saved.textContent='Grüße <b> & \\ud800'; document.title='  A\\n B  '; document.title",out));
    CHECK(!std::strcmp(out,"A B"));
    CHECK(!eval(e,"__reistDOM.take()",out)); CHECK(std::strstr(out,"00000007") && std::strstr(out,"efbfbd"));
    CHECK(!eval(e,"__reistDOM.take()",out)); CHECK(!out[0]);
    CHECK(!eval(e,"__reistDOM.sync('https://example.test/',[[9,0,0,2,0,'','',''],[1,1,1,3,0,'html','',''],[1,1,2,4,6,'head','',''],[1,1,3,5,0,'title','',''],[3,0,4,0,0,'','','A B'],[1,1,2,7,0,'body','',''],[1,1,6,8,0,'p','target',''],[3,0,7,0,0,'','','Changed']]); saved===document.getElementById('target') && saved.textContent==='Changed'",out));
    CHECK(!std::strcmp(out,"true"));
    CHECK(!eval(e,"saved.textContent=''; saved.textContent",out)); CHECK(!out[0]);
    CHECK(eval(e,"throw new Error('author')",out)==REIST_JS_EXCEPTION);
    CHECK(!eval(e,"document.title='After'; document.title",out)); CHECK(!std::strcmp(out,"After"));
    CHECK(!eval(e,"typeof document.write+'|'+typeof fetch+'|'+typeof setTimeout",out)); CHECK(!std::strcmp(out,"undefined|undefined|undefined"));
    CHECK(!eval(e,"__reistDOM.take(); try { saved.textContent='x'.repeat(40000) } catch(e) {}",out));
    CHECK(eval(e,"__reistDOM.take()",out)==REIST_JS_EXCEPTION);
    reist_js_destroy(&e); CHECK(!e); std::puts("BROWSER_BINDING_OK"); return 0;
}
#endif
