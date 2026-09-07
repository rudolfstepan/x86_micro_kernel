#include "browser_script.h"
#include "js_session.hpp"
#include <new>
#include <string.h>
using reist::browser::JsSession;
#if !__STDC_HOSTED__
__asm__(".pushsection .rodata.browser_dom,\"a\",@progbits\n"
        ".global browser_dom_data\nbrowser_dom_data:\n.incbin \"assets/browser/dom.js\"\n"
        ".global browser_dom_end\nbrowser_dom_end:\n.popsection\n");
#endif
extern "C" const char browser_dom_data[],browser_dom_end[];
struct script_record { uint32_t source_offset,source_length,patch_offset,patch_length; };
struct script_journal {
    uint32_t count,source_bytes,patch_bytes,complete;
    script_record records[BROWSER_SCRIPT_COUNT];
    char source[BROWSER_SCRIPT_SOURCE],patches[BROWSER_SCRIPT_JOURNAL];
};
struct browser_script_owner {
    JsSession js;
    script_journal *active=nullptr,*candidate=nullptr,*current=nullptr;
    char *wire=nullptr,*output=nullptr;
    browser_html_header_t parser{};
    browser_script_message_t message{};
    uint32_t endpoint=0,pid=0,generation=0,epoch=0,ordinal=0,received=0,total=0,sent=0,progress=0;
    // 0 idle, 1 receiving snapshot, 2 HELLO, 3 binding, 4 sync,
    // 5 author script, 6 journal, 7 response transfer.
    unsigned phase=0;
    uint32_t fixture=0,executions=0;
    int error=0,reflow=0,deny=0;
    static void clear(script_journal *j) { if(j) j->count=j->source_bytes=j->patch_bytes=j->complete=0; }
    void cancel() {
        if(endpoint) { (void)x86os_ipc_close(endpoint); endpoint=0; }
        js.cancel(); phase=0; received=total=sent=0;
    }
    int fail(int rc) { error=rc; cancel(); return rc; }
    int buffers() {
        if(!wire) wire=static_cast<char *>(x86os_malloc(BROWSER_SCRIPT_WIRE_MAX));
        if(!output) output=static_cast<char *>(x86os_malloc(BROWSER_SCRIPT_RESULT));
        if(!wire || !output) return fail(-12);
        return 0;
    }
    int evaluate(const char *source,uint32_t size,unsigned next) {
        if(js.evaluate(source,size,output,BROWSER_SCRIPT_RESULT)) return fail(-84);
        phase=next; return 0;
    }
    int reply(const char *data,uint32_t size) {
        if(size>=BROWSER_SCRIPT_RESULT) return fail(-84);
        if(data!=output && size) memcpy(output,data,size);
        message.magic=BROWSER_SCRIPT_REPLY; message.snapshot_length=0;
        message.source_length=size; message.size=sizeof(message)+size;
        phase=7; sent=0; return 0;
    }
    int admitted() {
        memcpy(&message,wire,sizeof(message));
        if(browser_script_message_valid(&message,total,&parser,pid,generation,ordinal+1,0)) return fail(-84);
        const char *source=wire+sizeof(message)+message.snapshot_length;
        if(current && ordinal<current->count) {
            const script_record &r=current->records[ordinal];
            if(r.source_length!=message.source_length || memcmp(current->source+r.source_offset,source,r.source_length)) return fail(-84);
            return reply(current->patches+r.patch_offset,r.patch_length);
        }
        if(reflow || (current && current->complete)) return fail(-84);
        if(!candidate) {
            candidate=static_cast<script_journal *>(x86os_malloc(sizeof(script_journal)));
            if(!candidate) return fail(-12); clear(candidate); current=candidate;
        }
        if(current->count!=ordinal || message.source_length>BROWSER_SCRIPT_SOURCE-current->source_bytes) return fail(-28);
        if(deny) return save(0);
        if(js.ready()) return evaluate(wire+sizeof(message),message.snapshot_length,4);
        if(js.busy() || js.pid() || epoch==UINT32_MAX) return fail(-84);
        uint64_t seed=0; if(x86os_monotonic_ms(&seed)) return fail(-84);
        seed^=(uint64_t)parser.parent_generation<<32; seed^=++epoch; if(!seed) seed=1;
        if(js.start(epoch,seed,fixture)) return fail(-84);
        x86os_puts("BROWSER_JS_WORKER pid="); x86os_print_number(js.pid());
        x86os_puts(" generation="); x86os_print_number((int)js.generation());
        x86os_puts(" fixture="); x86os_print_number((int)fixture); x86os_puts("\n");
        phase=2; return 0;
    }
    int save(uint32_t size) {
        browser_script_mutation_t mutations[BROWSER_SCRIPT_MUTATIONS]; uint32_t count=0,bytes=0;
        if(browser_script_journal(output,size,mutations,&count,&bytes) || !current ||
            ordinal!=current->count || size>BROWSER_SCRIPT_JOURNAL-current->patch_bytes) return fail(-84);
        script_record &r=current->records[ordinal];
        r={current->source_bytes,message.source_length,current->patch_bytes,size};
        memcpy(current->source+r.source_offset,wire+sizeof(message)+message.snapshot_length,r.source_length);
        if(size) memcpy(current->patches+r.patch_offset,output,size);
        current->source_bytes+=r.source_length; current->patch_bytes+=size; ++current->count;
        return reply(output,size);
    }
};
static_assert(sizeof(browser_script_owner)+2*sizeof(script_journal)+BROWSER_SCRIPT_WIRE_MAX+BROWSER_SCRIPT_RESULT<6U*1024U*1024U);
extern "C" browser_script_owner *browser_script_create() {
    void *storage=x86os_malloc(sizeof(browser_script_owner));
    return storage ? new(storage) browser_script_owner : nullptr;
}
extern "C" int browser_script_destroy(browser_script_owner *s) {
    if(!s) return 0;
    if(s->js.pid() || s->js.busy() || s->endpoint || s->phase) return -84;
    x86os_free(s->wire); x86os_free(s->output); x86os_free(s->active); x86os_free(s->candidate);
    s->~browser_script_owner(); x86os_free(s); return 0;
}
extern "C" void browser_script_cancel(browser_script_owner *s) { if(s) s->cancel(); }
extern "C" void browser_script_navigation(browser_script_owner *s) {
    if(!s) return; s->cancel(); s->clear(s->candidate); s->current=nullptr; s->error=0; s->deny=0;
}
extern "C" void browser_script_deny(browser_script_owner *s,int deny) { if(s) s->deny=!!deny; }
extern "C" void browser_script_fixture(browser_script_owner *s,uint32_t mode) { if(s && mode<=2) s->fixture=mode; }
extern "C" uint32_t browser_script_executions(const browser_script_owner *s) { return s?s->executions:0; }
extern "C" int browser_script_busy(const browser_script_owner *s) { return s && s->phase>=2; }
extern "C" int browser_script_ready(const browser_script_owner *s) { return !s || (!s->js.pid() && !s->js.busy()); }
extern "C" int browser_script_stranded(const browser_script_owner *s) { return s && s->js.state()==JsSession::State::stranded; }
extern "C" uint32_t browser_script_progress(const browser_script_owner *s) { return s ? s->progress+s->js.progress() : 0; }
extern "C" uint32_t browser_script_endpoint(const browser_script_owner *s) { return s ? s->endpoint : 0; }
extern "C" int browser_script_has_active(const browser_script_owner *s) { return s && s->active && s->active->count; }
extern "C" int browser_script_prepare(browser_script_owner *s,const browser_html_header_t *request,int reflow) {
    if(!s || !request || s->phase || s->endpoint || s->js.busy() || s->js.state()==JsSession::State::stranded) return -84;
    s->parser=*request; s->reflow=!!reflow; s->current=reflow?s->active:s->candidate;
    s->pid=s->generation=s->ordinal=s->received=s->total=0; s->error=0;
    return x86os_ipc_create(&s->endpoint);
}
extern "C" int browser_script_bind(browser_script_owner *s,uint32_t pid,uint32_t generation) {
    if(!s || !s->endpoint || !pid || !generation || s->pid) return -84;
    s->pid=pid; s->generation=generation;
    return x86os_ipc_delegate(s->endpoint,(int)pid,X86OS_IPC_RIGHT_RECEIVE);
}
extern "C" int browser_script_receive(browser_script_owner *s,const browser_css_packet_t *p,uint32_t length) {
    if(!s || !s->endpoint) return 0;
    if(s->error) return s->error;
    if(s->phase>=2) return s->fail(-84);
    if(!s->phase) {
        if(length<20 || length>sizeof(*p)) return s->fail(-84);
        uint32_t magic=0; memcpy(&magic,p->bytes,4);
        if(magic!=BROWSER_SCRIPT_MAGIC) return 0;
        if(length<16+sizeof(browser_script_message_t) || p->offset || p->total>BROWSER_SCRIPT_WIRE_MAX) return s->fail(-84);
        browser_script_message_t h; memcpy(&h,p->bytes,sizeof(h));
        if(browser_script_message_valid(&h,p->total,&s->parser,s->pid,s->generation,s->ordinal+1,0) || s->buffers()) return s->fail(-84);
        s->phase=1;
    }
    if(browser_css_packet_accept(p,length,s->parser.request,(uint8_t *)s->wire,BROWSER_SCRIPT_WIRE_MAX,&s->received,&s->total)) return s->fail(-84);
    ++s->progress;
    if(s->received==s->total && s->admitted()) return s->error;
    return 1;
}
extern "C" int browser_script_poll(browser_script_owner *s) {
    if(!s) return 0;
    s->js.poll();
    if(s->js.state()==JsSession::State::stranded) return s->fail(-84);
    if(s->phase>=2 && s->phase<=6) {
        if(s->js.busy()) return 0;
        if(!s->js.ready()) return s->fail(s->js.error()?s->js.error():-84);
        if(s->phase!=5 && s->js.engine_status()) return s->fail(-84);
        switch(s->phase) {
        case 2: return s->evaluate(browser_dom_data,(uint32_t)(browser_dom_end-browser_dom_data),3);
        case 3: return s->evaluate(s->wire+sizeof(s->message),s->message.snapshot_length,4);
        case 4: ++s->executions; return s->evaluate(s->wire+sizeof(s->message)+s->message.snapshot_length,s->message.source_length,5);
        case 5:
            if(s->js.engine_status()) x86os_puts("BROWSER_JS_EXCEPTION\n");
            return s->evaluate("__reistDOM.take()",17,6);
        case 6: if(!s->js.result()) return s->fail(-84); return s->save(s->js.result_length());
        }
    }
    if(s->phase==7) for(unsigned i=0;i<8;++i) {
        browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,s->parser.request,s->sent,s->message.size,{0}};
        uint32_t n=s->message.size-s->sent; if(n>BROWSER_CSS_PACKET_DATA) n=BROWSER_CSS_PACKET_DATA;
        for(uint32_t j=0;j<n;++j) {
            uint32_t at=s->sent+j;
            p.bytes[j]=at<sizeof(s->message) ? ((const uint8_t *)&s->message)[at] : (uint8_t)s->output[at-sizeof(s->message)];
        }
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),16+n,{0}};
        memcpy(m.payload,&p,m.length);
        int rc=x86os_ipc_send_bulk_timeout(s->endpoint,&m,0);
        if(rc==-11) return 0; if(rc) return s->fail(rc);
        s->sent+=n; ++s->progress;
        if(s->sent==s->message.size) { s->phase=0; s->received=s->total=0; ++s->ordinal; break; }
    }
    return s->error;
}
extern "C" int browser_script_finish_parse(browser_script_owner *s) {
    if(!s || !s->endpoint) return 0;
    int result=s->error || s->phase || (s->current && s->ordinal!=s->current->count) ? -84 : 0;
    if(!result && s->current) s->current->complete=1;
    if(s->js.ready()) { if(s->js.shutdown()) result=-84; }
    (void)x86os_ipc_close(s->endpoint); s->endpoint=0;
    if(result) s->cancel(); return result;
}
extern "C" void browser_script_commit(browser_script_owner *s,int reflow) {
    if(!s || reflow) return;
    auto *old=s->active; s->active=s->candidate; s->candidate=old;
    s->clear(s->candidate); s->current=nullptr;
}
