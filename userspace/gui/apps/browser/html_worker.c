/* One bounded, disposable Ring-3 HTML5 generation. No network or GUI calls. */
#include "x86os.h"
#include "html_engine.h"
#include "html_protocol.h"
#include <string.h>
static struct { browser_html_header_t header; uint8_t bytes[REIST_HTML_INPUT_CAPACITY]; } input;
static browser_html_reply_t reply;
static uint8_t wire_reply[sizeof(reply)];
#ifdef REIST_CSS_WORKER
#include "css_engine.h"
static struct { browser_css_request_t request; uint8_t bytes[65536]; } css_input;
static browser_scene_t css_scene;
static uint8_t css_wire[BROWSER_CSS_WIRE_CAPACITY];
static int css_worker(const char *number) {
    uint32_t endpoint=0;
    for (uint32_t i=0;number[i];++i) {
        if (i>=10 || number[i]<'0' || number[i]>'9' ||
            endpoint>(UINT32_MAX-(uint32_t)(number[i]-'0'))/10) return 64;
        endpoint=endpoint*10+(uint32_t)(number[i]-'0');
    }
    if (!endpoint) return 64;
    uint32_t deadline=x86os_uptime_ms()+BROWSER_HTML_DEADLINE_MS;
    uint32_t at=0,total=0,request=0;
    for (;;) {
        int32_t left=(int32_t)(deadline-x86os_uptime_ms()); if (left<=0) return 74;
        x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),0,{0}};
        int rc=x86os_ipc_receive_bulk_timeout(endpoint,&message,(uint32_t)left);
        /* Parent can delegate only after spawn returns: a not-yet-held handle
         * is EBADF. Retry only before the first accepted packet; later loss of
         * authority must abort. Never extend this generation's deadline. */
        if (!at && (rc==-9 || rc==-13)) {
            if (x86os_sleep_ms(1)) return 74;
            continue;
        }
        if (rc || message.version!=X86OS_IPC_BULK_MESSAGE_VERSION || message.struct_size!=sizeof(message)) return 74;
        browser_css_packet_t packet; memcpy(&packet,message.payload,sizeof(packet));
        if (!at) request=packet.request;
        if (browser_css_packet_accept(&packet,message.length,request,(uint8_t *)&css_input,sizeof(css_input),&at,&total)) return 74;
        if (at==total) break;
    }
    const browser_css_request_t *q=&css_input.request;
    if (browser_css_request_validate(q) || q->header.size!=total || q->header.request!=request) return 74;
    if (q->header.mode==1) { __asm__ volatile("ud2"); return 70; }
    if (q->header.mode==2) { (void)x86os_sleep_ms(BROWSER_HTML_DEADLINE_MS+1000); return 70; }
    if (browser_css_render(css_input.bytes,q->header.input_length,q->width,q->height,q->image_sizes,q->document_url,&reply.document,&css_scene)) return 65;
    x86os_process_identity_t identity;
    if (x86os_process_identity_of(x86os_getpid(),&identity) || identity.version!=1 ||
        identity.struct_size!=sizeof(identity) || identity.pid!=x86os_getpid() || !identity.generation) return 70;
    reply.header=q->header; reply.header.size=sizeof(reply);
    reply.header.child_pid=(uint32_t)identity.pid; reply.header.child_generation=identity.generation;
    int size=browser_css_pack(&reply,&css_scene,css_wire,sizeof(css_wire)); if (size<0) return 65;
    for (at=0;at<(uint32_t)size;) {
        int32_t left=(int32_t)(deadline-x86os_uptime_ms()); if (left<=0) return 74;
        uint32_t n=(uint32_t)size-at; if (n>BROWSER_CSS_PACKET_DATA) n=BROWSER_CSS_PACKET_DATA;
        browser_css_packet_t packet={BROWSER_CSS_PACKET_MAGIC,request,at,(uint32_t)size,{0}};
        memcpy(packet.bytes,css_wire+at,n);
        x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),16+n,{0}};
        memcpy(message.payload,&packet,16+n);
        if (x86os_ipc_send_bulk_timeout(endpoint,&message,(uint32_t)left)) return 74;
        at+=n;
    }
    /* The owner retains the endpoint until the final packet is drained and this
     * exact generation has been reaped. Child exit revokes only its own rights. */
    return 0;
}
#endif
static void phase(const char *name) {
    x86os_puts("HTMLWORK_PHASE "); x86os_puts(name); x86os_puts(" ms=");
    x86os_print_number((int)x86os_uptime_ms()); x86os_puts("\n");
}
static int read_exact(int fd, void *buffer, uint32_t length) {
    uint8_t *bytes=buffer;
    for (uint32_t done=0; done<length;) {
        uint32_t n=length-done; if (n>4096U) n=4096U;
        int rc=x86os_read(fd,bytes+done,n);
        if (rc<=0 || (uint32_t)rc>n) return -5;
        done+=(uint32_t)rc;
    }
    return 0;
}
int main(int argc, char **argv) {
    if (argc!=3 || !argv || !argv[1] || !argv[2]) return 64;
#ifdef REIST_CSS_WORKER
    if (!strcmp(argv[1],"--ipc")) return css_worker(argv[2]);
#endif
    phase("open-input");
    int fd=x86os_open(argv[1]); if (fd<0) return 74;
    int rc=read_exact(fd,&input.header,sizeof(input.header));
    browser_html_header_t *h=&input.header;
    if (!rc && (h->magic!=BROWSER_HTML_MAGIC || h->version!=BROWSER_HTML_VERSION ||
        !h->request || !h->parent_pid || !h->parent_generation || h->child_pid ||
        h->child_generation || !h->input_length || h->input_length>sizeof(input.bytes) ||
        h->size!=sizeof(*h)+h->input_length || h->mode>2U || h->reserved[0] || h->reserved[1])) rc=-84;
    if (!rc) rc=read_exact(fd,input.bytes,h->input_length);
    uint8_t extra;
    if (!rc && x86os_read(fd,&extra,1)!=0) rc=-84;
    int closed=x86os_close(fd); if (rc || closed) return 74;
    phase("input-closed");
    /* Explicit local fault-injection requests; never inferred from HTML/URLs. */
    if (h->mode==1U) { __asm__ volatile("ud2"); return 70; }
    if (h->mode==2U) { (void)x86os_sleep_ms(BROWSER_HTML_DEADLINE_MS+1000U); return 70; }
    if (browser_html5_parse(input.bytes,h->input_length,&reply.document)!=0) return 65;
    phase("parsed");
    x86os_process_identity_t identity;
    if (x86os_process_identity_of(x86os_getpid(),&identity)!=0 || identity.version!=1U ||
        identity.struct_size!=sizeof(identity) || identity.pid!=x86os_getpid() || !identity.generation) return 70;
    reply.header=*h; reply.header.size=sizeof(reply);
    reply.header.child_pid=(uint32_t)identity.pid; reply.header.child_generation=identity.generation;
    if (browser_html_validate(&reply,sizeof(reply),h,(uint32_t)identity.pid,identity.generation)!=0) return 65;
    int packed=browser_html_pack(&reply,wire_reply,sizeof(wire_reply));
    if (packed<0) return 65;
    /* No reader until this generation is reaped. A failed/partial write is never
     * committed by the parent; no rename or additional publication path needed. */
    fd=x86os_create(argv[2]); if (fd<0) return 74;
    phase("output-created");
    rc=0;
    for (uint32_t done=0; done<(uint32_t)packed;) {
        uint32_t n=(uint32_t)packed-done; if (n>4096U) n=4096U;
        int written=x86os_write(fd,wire_reply+done,n);
        if (written<=0 || (uint32_t)written>n) { rc=-5; break; }
        done+=(uint32_t)written;
    }
    phase("output-written");
    closed=x86os_close(fd);
    phase("output-closed");
    return rc || closed ? 74 : 0;
}
