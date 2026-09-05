/* One bounded, disposable Ring-3 HTML5 generation. No network or GUI calls. */
#include "x86os.h"
#include "html_engine.h"
#include "html_protocol.h"
#include <string.h>
static struct { browser_html_header_t header; uint8_t bytes[REIST_HTML_INPUT_CAPACITY]; } input;
static browser_html_reply_t reply;
static uint8_t wire_reply[sizeof(reply)];
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
