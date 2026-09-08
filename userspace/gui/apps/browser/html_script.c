/* HTMLWORK only: real parser callback, blocking solely inside the disposable
 * parser process. BROWSER directly owns the separately executing JSWORK. */
#include "html_engine.h"
#include "script_protocol.h"
#include "browser_scene.h"
#include "x86os.h"
#include <string.h>
static struct {
    uint32_t outgoing,incoming,deadline,ordinal;
    browser_html_header_t request;
    x86os_process_identity_t identity;
    const char *url;
    char *buffer;
} scripting;
static int remaining(uint32_t *left) {
    int32_t n=(int32_t)(scripting.deadline-x86os_uptime_ms());
    if(n<=0) return -110; *left=(uint32_t)n; return 0;
}
static int execute(void *unused,node *element) {
    (void)unused;
    if(scripting.ordinal==BROWSER_SCRIPT_COUNT) return -28;
    if(!scripting.buffer) scripting.buffer=x86os_malloc(BROWSER_SCRIPT_WIRE_MAX);
    if(!scripting.buffer) return -12;
    browser_script_message_t *h=(browser_script_message_t *)scripting.buffer;
    char *data=scripting.buffer+sizeof(*h); uint32_t snapshot=0,source=0;
    int rc=browser_html_script_snapshot_version(element,scripting.url,data,
        BROWSER_SCRIPT_SNAPSHOT+BROWSER_SCRIPT_SOURCE,&snapshot,&source,BROWSER_SCRIPT_EXTERNAL_VERSION);
    if(rc) return rc;
    uint32_t kind=browser_html_script_source(element)!=NULL;
    *h=(browser_script_message_t){BROWSER_SCRIPT_MAGIC,BROWSER_SCRIPT_EXTERNAL_VERSION,sizeof(*h)+snapshot+source,
        scripting.request.request,scripting.request.parent_pid,scripting.request.parent_generation,
        (uint32_t)scripting.identity.pid,scripting.identity.generation,++scripting.ordinal,snapshot,source,kind};
    for(uint32_t offset=0;offset<h->size;) {
        uint32_t left=0; if(remaining(&left)) return -110;
        browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,h->request,offset,h->size,{0}};
        uint32_t n=h->size-offset; if(n>BROWSER_CSS_PACKET_DATA) n=BROWSER_CSS_PACKET_DATA;
        memcpy(p.bytes,scripting.buffer+offset,n);
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),16+n,{0}};
        memcpy(m.payload,&p,m.length);
        if(x86os_ipc_send_bulk_timeout(scripting.outgoing,&m,left)) return -84;
        offset+=n;
    }
    uint32_t offset=0,total=0;
    do {
        uint32_t left=0; if(remaining(&left)) return -110;
        x86os_ipc_bulk_message_t m={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(m),0,{0}};
        if(x86os_ipc_receive_bulk_timeout(scripting.incoming,&m,left) ||
            m.version!=X86OS_IPC_BULK_MESSAGE_VERSION || m.struct_size!=sizeof(m)) return -84;
        browser_css_packet_t p; memcpy(&p,m.payload,sizeof(p));
        if(browser_css_packet_accept(&p,m.length,scripting.request.request,(uint8_t *)scripting.buffer,
            sizeof(*h)+BROWSER_SCRIPT_RESULT-1,&offset,&total)) return -84;
    } while(offset<total);
    if(browser_script_message_valid(h,total,&scripting.request,(uint32_t)scripting.identity.pid,
        scripting.identity.generation,scripting.ordinal,1) || h->version!=BROWSER_SCRIPT_EXTERNAL_VERSION || h->reserved!=kind) return -84;
    return browser_html_script_apply_version(data,h->source_length,BROWSER_SCRIPT_ATTRIBUTE_VERSION);
}
int browser_html_script_setup(uint32_t outgoing,uint32_t incoming,const browser_html_header_t *h,uint32_t deadline,const char *url) {
    if(!h || !outgoing || !incoming || outgoing==incoming || !url || scripting.buffer ||
       h->version!=BROWSER_HTML_SCRIPT_VERSION || h->reserved[1]!=incoming) return -84;
    memset(&scripting,0,sizeof(scripting));
    if(x86os_process_identity_of(x86os_getpid(),&scripting.identity) ||
       scripting.identity.version!=1 || scripting.identity.struct_size!=sizeof(scripting.identity) ||
       scripting.identity.pid!=x86os_getpid() || !scripting.identity.generation) return -84;
    scripting.outgoing=outgoing; scripting.incoming=incoming; scripting.request=*h;
    scripting.deadline=deadline; scripting.url=url;
    browser_html_script_external_enable(1); browser_html_script_hook_set(execute,NULL); return 0;
}
void browser_html_script_finish(void) {
    browser_html_script_hook_set(NULL,NULL);
    browser_html_script_external_enable(0);
    if(scripting.buffer) x86os_free(scripting.buffer);
    memset(&scripting,0,sizeof(scripting));
}
