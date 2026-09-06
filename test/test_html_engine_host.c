#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <reist/libc.h>
#include "userspace/gui/apps/browser/html_engine.h"
#include "test_html_worker_host.c"

static reist_html_document_t doc;
extern _Noreturn void _Exit(int);
_Noreturn void reist_libc_fail(unsigned code) { (void)code; _Exit(70); }
static _Alignas(max_align_t) uint8_t legacy_backing[REIST_LIBC_HEAP_LIMIT];
static size_t legacy_used;
static unsigned legacy_initializations,legacy_oom;
static void *legacy_acquire(void *unused,size_t size) {
    (void)unused;
    if(legacy_oom || size>sizeof(legacy_backing)-legacy_used) return NULL;
    void *p=legacy_backing+legacy_used; legacy_used+=size; return p;
}
static void legacy_release(void *unused,void *p,size_t size) {
    (void)unused;
    assert((uint8_t *)p>=legacy_backing && (uint8_t *)p+size<=legacy_backing+legacy_used);
}
int reist_libc_init_process(size_t budget) {
    assert(budget==REIST_LIBC_HEAP_LIMIT); ++legacy_initializations;
    reist_libc_backing_t backing={1,sizeof(backing),(uint32_t)budget,256U*1024U,NULL,legacy_acquire,legacy_release};
    return reist_libc_init_backing(&backing);
}
static const char *find(const char *text, const char *part) {
    size_t length=strlen(part);
    for (; *text; ++text) if (!strncmp(text,part,length)) return text;
    return NULL;
}
int main(int argc, char **argv) {
    if(argc>1 && !strcmp(argv[1],"legacy-oom")) {
        legacy_oom=1;
        assert(browser_html5_parse((const uint8_t *)"<p>x</p>",8,&doc)<0);
        assert(legacy_initializations==1 && !legacy_used);
        puts("HTML5_LAZY_LEGACY_OOM_OK"); return 0;
    }
    if (argc>1 && !strcmp(argv[1],"fixture")) {
        uint8_t bytes[65536]; FILE *f=fopen("htdocs/browser-test.html","rb"); assert(f);
        size_t n=fread(bytes,1,sizeof(bytes),f); fclose(f);
        int parsed=browser_html5_parse(bytes,n,&output_reply.document);
        assert(parsed==0);
        browser_html_header_t h={BROWSER_HTML_MAGIC,BROWSER_HTML_VERSION,sizeof(h)+(uint32_t)n,12,77,22,0,0,(uint32_t)n,0,{0,0}};
        output_reply.header=h; output_reply.header.size=sizeof(output_reply);
        output_reply.header.child_pid=81; output_reply.header.child_generation=23;
        int validated=browser_html_validate(&output_reply,sizeof(output_reply),&h,81,23);
        assert(!validated);
        int packed=browser_html_pack(&output_reply,output_wire,sizeof(output_wire));
        assert(packed>0 && packed<8192);
        printf("HTML5_FIXTURE_WIRE_BYTES=%d\n",packed);
        puts("HTML5_FIXTURE_OK"); return 0;
    }
    if (argc>1 && !strcmp(argv[1],"charset")) {
        const char html[]="<meta charset='iso-8859-1'><p>not a supported decoder</p>";
        assert(browser_html5_parse((const uint8_t *)html,sizeof(html)-1,&doc)==-84);
        puts("HTML5_CHARSET_REJECT_OK"); return 0;
    }
    if (argc>1 && strcmp(argv[1],"quota")) { worker_test(argv[1]); return 0; }
    if (argc>1) {
        static char large[65536];
        for (unsigned i=0;i<sizeof(large)/8;++i) memcpy(large+i*8,"<b>x</b>",8);
        assert(browser_html5_parse((const uint8_t *)large,sizeof(large),&doc)==-28);
        puts("HTML5_QUOTA_OK"); return 0;
    }
    const char *html = "<!doctype html><title>A &copy; &euro;</title>"
        "<p>one<p>two <b>bold<i>both</b>italic</i>"
        "<table>outside<tr><td>cell</table><script>if(a<b) secret()</script>"
        "<a href='?q=a&amp;b=2'>link &NotEqualTilde;</a><img src='x.png' alt='pic'>";
    assert(browser_html5_parse((const uint8_t *)html, strlen(html), &doc)==0);
    assert(find(doc.title,"\xc2\xa9") && find(doc.title,"\xe2\x82\xac"));
    assert(find(doc.text,"one") && find(doc.text,"two"));
    assert(!find(doc.text,"secret"));
    assert(find(doc.text,"outside") < find(doc.text,"cell"));
    assert(doc.link_count==1 && !strcmp(doc.links[0].href,"?q=a&b=2"));
    assert(doc.image_count==1 && !strcmp(doc.images[0].source,"x.png"));
    assert(legacy_initializations==1 && legacy_used && legacy_used<=REIST_LIBC_HEAP_LIMIT);
    puts("HTML5_TREE_PROJECTION_OK");
    return 0;
}
