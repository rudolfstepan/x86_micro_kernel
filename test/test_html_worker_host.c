/* Included by the engine fixture: real worker entry and protocol, mocked syscalls. */
#define main html_worker_main
#include "userspace/gui/apps/browser/html_worker.c"
#undef main
static uint8_t request_bytes[sizeof(browser_html_header_t)+65536U];
static browser_html_reply_t output_reply;
static uint8_t output_wire[sizeof(output_reply)];
static uint32_t request_size, read_at, written_bytes, closed_count, created_count;
static int write_failure;
int x86os_getpid(void) { return 81; }
void x86os_puts(const char *s) { (void)s; }
void x86os_print_number(int n) { (void)n; }
uint32_t x86os_uptime_ms(void) { return 100; }
int x86os_process_identity_of(int pid, x86os_process_identity_t *id) {
    assert(pid==81); *id=(x86os_process_identity_t){1,sizeof(*id),81,23}; return 0;
}
int x86os_open(const char *path) { assert(!strcmp(path,"/in")); return 3; }
int x86os_create(const char *path) { assert(!strcmp(path,"/out")); ++created_count; return 4; }
int x86os_read(int fd, void *bytes, size_t n) {
    assert(fd==3 && n<=4096); if (n>request_size-read_at) n=request_size-read_at;
    if (n>7) n=7; /* Fragmented I/O must make bounded progress. */
    memcpy(bytes,request_bytes+read_at,n); read_at+=(uint32_t)n; return (int)n;
}
int x86os_write(int fd, const void *bytes, size_t n) {
    assert(fd==4 && n<=4096); if (write_failure) return -5;
    if (n>79) n=79;
    assert(n<=sizeof(output_reply)-written_bytes);
    memcpy(output_wire+written_bytes,bytes,n); written_bytes+=(uint32_t)n; return (int)n;
}
int x86os_close(int fd) { assert(fd==3 || fd==4); ++closed_count; return 0; }
int x86os_sleep_ms(uint32_t ms) { assert(ms==6000); return 0; }
static void worker_test(const char *mode) {
    const char html[]="<title>Worker</title><p>safe &copy;<img src='x.png'>";
    browser_html_header_t q={BROWSER_HTML_MAGIC,BROWSER_HTML_VERSION,sizeof(q)+sizeof(html)-1,12,77,22,0,0,sizeof(html)-1,0,{0,0}};
    request_size=q.size;
    memcpy(request_bytes,&q,sizeof(q)); memcpy(request_bytes+sizeof(q),html,sizeof(html)-1);
    if (!strcmp(mode,"bad-request")) request_bytes[0]^=1;
    if (!strcmp(mode,"truncated")) --request_size;
    if (!strcmp(mode,"extra")) request_bytes[request_size++]=0;
    if (!strcmp(mode,"write-fail")) write_failure=1;
    char *argv[]={"htmlwork","/in","/out"};
    int result=html_worker_main(3,argv);
    if (strcmp(mode,"worker")) {
        assert(result==74 && closed_count==(write_failure ? 2U : 1U));
        assert(created_count==(write_failure ? 1U : 0U));
    } else {
        assert(!result && closed_count==2 && created_count==1 && written_bytes<2048U);
        uint32_t wire_length=written_bytes;
        assert(!browser_html_unpack(output_wire,wire_length,&output_reply));
        assert(browser_html_unpack(output_wire,wire_length-1,&output_reply)==-84);
        assert(browser_html_unpack(output_wire,wire_length+1,&output_reply)==-84);
        uint32_t bad=UINT32_MAX;
        for (unsigned i=0; i<5; ++i) {
            size_t offset=sizeof(browser_html_header_t)+128U+4U*i;
            uint32_t saved; memcpy(&saved,output_wire+offset,4); memcpy(output_wire+offset,&bad,4);
            assert(browser_html_unpack(output_wire,wire_length,&output_reply)==-84);
            memcpy(output_wire+offset,&saved,4);
        }
        assert(!browser_html_unpack(output_wire,wire_length,&output_reply));
        written_bytes=sizeof(output_reply); /* Existing semantic/envelope checks. */
        assert(!browser_html_validate(&output_reply,written_bytes,&q,81,23));
        assert(!strcmp(output_reply.document.title,"Worker"));
        /* Reject every envelope field corruption, truncation and foreign generation. */
        uint32_t *fields=(uint32_t *)&output_reply.header;
        for (unsigned i=0; i<sizeof(q)/4; ++i) {
            uint32_t saved=fields[i]; fields[i]^=0x80000000U;
            assert(browser_html_validate(&output_reply,written_bytes,&q,81,23)==-84); fields[i]=saved;
        }
        assert(browser_html_validate(&output_reply,written_bytes-1,&q,81,23)==-84);
        assert(browser_html_validate(&output_reply,written_bytes,&q,82,23)==-84);
        assert(browser_html_validate(&output_reply,written_bytes,&q,81,24)==-84);
        reist_html_document_t *d=&output_reply.document;
        uint32_t length=d->text_length; d->text_length=65537; assert(browser_html_validate(&output_reply,written_bytes,&q,81,23)==-84); d->text_length=length;
        uint32_t kind=d->elements[0].kind; d->elements[0].kind=99; assert(browser_html_validate(&output_reply,written_bytes,&q,81,23)==-84); d->elements[0].kind=kind;
        d->elements[0].text_offset=UINT32_MAX; assert(browser_html_validate(&output_reply,written_bytes,&q,81,23)==-84);
        assert(browser_html_pack(&output_reply,output_wire,sizeof(output_wire))==-84);
        /* Full-capacity replies stay bounded, even if no prefix can be elided. */
        memset(&output_reply,0,sizeof(output_reply)); output_reply.header=q;
        output_reply.header.size=sizeof(output_reply); output_reply.header.child_pid=81;
        output_reply.header.child_generation=23;
        d=&output_reply.document; d->text_length=REIST_HTML_TEXT_CAPACITY;
        memset(d->text,'a',sizeof(d->text)); d->element_count=REIST_HTML_ELEMENT_CAPACITY;
        d->link_count=REIST_HTML_LINK_CAPACITY; d->image_count=REIST_HTML_IMAGE_CAPACITY;
        d->anchor_count=REIST_HTML_ANCHOR_CAPACITY;
        for (unsigned i=0; i<d->element_count; ++i)
            d->elements[i]=(reist_html_element_t){REIST_HTML_ELEMENT_TEXT,i,1,0,UINT32_MAX,0,0};
        memset(output_wire,0x5a,sizeof(output_wire));
        assert(browser_html_pack(&output_reply,output_wire,sizeof(output_wire)-1)==-28);
        assert(output_wire[0]==0x5a && output_wire[sizeof(output_wire)-1]==0x5a);
        assert(browser_html_pack(&output_reply,output_wire,sizeof(output_wire))==sizeof(output_wire));
        assert(!browser_html_unpack(output_wire,sizeof(output_wire),&output_reply));
        assert(!browser_html_validate(&output_reply,sizeof(output_reply),&q,81,23));
    }
    puts("HTML5_WORKER_PROTOCOL_OK");
}
