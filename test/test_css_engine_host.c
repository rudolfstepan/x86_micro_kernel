#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <reist/libc.h>
#include "userspace/gui/apps/browser/css_engine.h"
#define REIST_CSS_WORKER
#define main static html_worker_main
#include "userspace/gui/apps/browser/html_worker.c"
#undef main
extern _Noreturn void _Exit(int);
#undef assert
#define assert(expr) do { if (!(expr)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); _Exit(1); } } while (0)
static reist_html_document_t document;
static browser_resources_t resource_bundle;
static browser_resource_needs_t resource_needs;
static _Alignas(max_align_t) uint8_t private_heap[32U*1024U*1024U];
static size_t private_used;
static unsigned private_fail;
static struct {
    uint64_t before;
    _Alignas(max_align_t) uint8_t bytes[sizeof(css_worker_buffers_t)];
    uint64_t after;
} transfer_memory;
static unsigned transfer_allocs,transfer_frees,transfer_live,transfer_fail,transfer_delay;
static void *private_acquire(void *unused,size_t size) {
    (void)unused; if(private_fail || size>sizeof(private_heap)-private_used) return NULL;
    void *p=private_heap+private_used; private_used+=size; return p;
}
static void private_release(void *unused,void *p,size_t size) {
    (void)unused; assert((uint8_t *)p>=private_heap && (uint8_t *)p+size<=private_heap+private_used);
}
int reist_libc_init_process(size_t budget) {
    assert(budget==sizeof(private_heap) || budget==REIST_LIBC_HEAP_LIMIT); private_used=0;
    reist_libc_backing_t backing={1,sizeof(backing),(uint32_t)budget,256U*1024U,NULL,private_acquire,private_release};
    return reist_libc_init_backing(&backing);
}
static browser_scene_t scene;
static uint8_t transport[BROWSER_CSS_WIRE_CAPACITY], request_wire[sizeof(browser_css_request_t)+BROWSER_CSS_INPUT_BYTES];
static uint32_t sent, received, request_length, transport_length, time_ms, bad_packet;
static uint32_t sleeps, no_delegation, revoke_after_packet, sleep_failure;
static int startup_error=-9;
int x86os_getpid(void) { return 81; }
uint32_t x86os_uptime_ms(void) { return time_ms++; }
void *x86os_malloc(size_t size) {
    assert(!transfer_live && size && size<=sizeof(transfer_memory.bytes));
    ++transfer_allocs; time_ms+=transfer_delay;
    if(transfer_fail) return NULL;
    transfer_live=1; transfer_memory.before=transfer_memory.after=0x123456789ABCDEF0ULL;
    memset(transfer_memory.bytes,0xa5,sizeof(transfer_memory.bytes));
    return transfer_memory.bytes;
}
void x86os_free(void *p) {
    assert(transfer_live && p==transfer_memory.bytes);
    assert(transfer_memory.before==0x123456789ABCDEF0ULL && transfer_memory.after==0x123456789ABCDEF0ULL);
    ++transfer_frees; transfer_live=0;
}
void x86os_puts(const char *s) { (void)s; }
void x86os_print_number(int n) { (void)n; }
int x86os_process_identity_of(int pid,x86os_process_identity_t *id) {
    assert(pid==81); *id=(x86os_process_identity_t){1,sizeof(*id),81,23}; return 0;
}
int x86os_sleep_ms(uint32_t n) { assert(n==1); ++sleeps; time_ms+=n; return sleep_failure ? -5 : 0; }
int x86os_open(const char *s) { (void)s; assert(0); return -5; }
int x86os_create(const char *s) { (void)s; assert(0); return -5; }
int x86os_read(int fd,void *p,size_t n) { (void)fd;(void)p;(void)n;assert(0);return -5; }
int x86os_write(int fd,const void *p,size_t n) { (void)fd;(void)p;(void)n;assert(0);return -5; }
int x86os_close(int fd) { (void)fd;assert(0);return -5; }
int x86os_ipc_receive_bulk_timeout(x86os_ipc_handle_t h,x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    assert(h==42 && timeout && timeout<=5000);
    if (no_delegation || time_ms<3 || (sent && revoke_after_packet)) return startup_error;
    uint32_t n=request_length-sent; if(n>53) n=53;
    assert(n); browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,12,sent,request_length,{0}};
    memcpy(p.bytes,request_wire+sent,n); if(bad_packet) ++p.offset;
    memcpy(m->payload,&p,16+n); m->length=16+n; sent+=n; return 0;
}
int x86os_ipc_send_bulk_timeout(x86os_ipc_handle_t h,const x86os_ipc_bulk_message_t *m,uint32_t timeout) {
    assert(h==42 && timeout && timeout<=5000 && sent==request_length);
    browser_css_packet_t p; memcpy(&p,m->payload,sizeof(p));
    assert(!browser_css_packet_accept(&p,m->length,12,transport,sizeof(transport),&received,&transport_length)); return 0;
}
_Noreturn void reist_libc_fail(unsigned code) { (void)code; _Exit(70); }
static const browser_scene_run_t *text_run(const char *text) {
    for (uint32_t i=0;i<scene.count;++i) {
        const browser_scene_run_t *r=&scene.runs[i];
        if (r->kind==1 && r->length==strlen(text) && !memcmp(document.text+r->offset,text,r->length)) return r;
    }
    return NULL;
}
int main(int argc, char **argv) {
    const char *mode=argc>1 ? argv[1] : "cascade";
    if(!strcmp(mode,"public-utf8-bom") || !strcmp(mode,"public-transport") || !strcmp(mode,"public-utf16") ||
        !strcmp(mode,"public-opaque") || !strcmp(mode,"public-oom")) {
        static uint8_t bytes[4096]; size_t n=0;
        const char *html="<meta charset=ISO-8859-1><p>caf\xc3\xa9</p>";
        uint32_t encoding=BROWSER_ENCODING_UTF8;
        if(!strcmp(mode,"public-utf8-bom")) {
            memcpy(bytes,"\xef\xbb\xbf",3); n=3; encoding=BROWSER_ENCODING_WINDOWS1252;
        }
        memcpy(bytes+n,html,strlen(html)); n+=strlen(html);
        if(!strcmp(mode,"public-utf16")) {
            html="<p>wide</p>"; n=2; bytes[0]=255; bytes[1]=254;
            for(size_t i=0;html[i];++i) { bytes[n++]=(uint8_t)html[i]; bytes[n++]=0; }
        }
        if(!strcmp(mode,"public-opaque")) {
            char long_url[401]; const char prefix_url[]="https://example.test/style?q=";
            memcpy(long_url,prefix_url,sizeof(prefix_url));
            size_t prefix=strlen(long_url); memset(long_url+prefix,'x',400-prefix); long_url[400]=0;
            n=(size_t)snprintf((char *)bytes,sizeof(bytes),
                "<link rel=stylesheet href='%s'><style>p{background-image:url(data:image/png,%s)}</style>"
                "<p><a href='%s'>visible</a></p><img src='%s' alt='placeholder'>",
                long_url,long_url,long_url,long_url);
            browser_resources_init(&resource_bundle,1);
            assert(browser_resources_add(&resource_bundle,"https://example.test/",long_url,0)==0);
            /* Author rule targets the link itself: its UA color is a specified
             * child value, not an inherited paragraph color. */
            assert(!browser_resources_store(&resource_bundle,0,long_url,(const uint8_t *)"a{color:#123456}",16));
        }
        private_fail=!strcmp(mode,"public-oom");
        int rc=browser_css_render_document(bytes,n,800,500,NULL,"https://example.test/",
            !strcmp(mode,"public-opaque") ? &resource_bundle : NULL,&resource_needs,&document,&scene,encoding);
        if(private_fail) assert(rc==-12);
        else {
            assert(!rc);
            if(!strcmp(mode,"public-opaque")) {
                assert(text_run("visible") && !document.link_count && document.image_count==1);
                assert(!document.images[0].source[0] && !resource_needs.count);
                assert(strlen(scene.image_urls[0])==400 && text_run("visible")->color==0xff123456);
            } else assert(text_run(!strcmp(mode,"public-utf16") ? "wide" : "caf\xc3\xa9"));
        }
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strcmp(mode,"public-file")) {
        static uint8_t bytes[BROWSER_DOCUMENT_INPUT_CAPACITY],css[BROWSER_RESOURCE_LIMIT];
        assert(argc>=3); FILE *f=fopen(argv[2],"rb"); assert(f);
        size_t n=fread(bytes,1,sizeof(bytes),f); assert(!ferror(f) && feof(f)); fclose(f);
        browser_resources_init(&resource_bundle,1);
        const char *url=argc>=4 ? "https://intracom.at/" : "https://www.google.com/";
        if(argc>=4) {
            f=fopen(argv[3],"rb"); assert(f); size_t size=fread(css,1,sizeof(css),f); fclose(f);
            assert(browser_resources_add(&resource_bundle,url,"https://intracom.at/assets/css/site.css",0)==0);
            assert(!browser_resources_store(&resource_bundle,0,"https://intracom.at/assets/css/site.css",css,(uint32_t)size));
            const char *font_css="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Source+Serif+4:ital,wght@0,400;0,600;0,700;1,400;1,600&display=swap";
            int index=browser_resources_add(&resource_bundle,url,font_css,0); assert(index>=0);
            assert(!browser_resources_store(&resource_bundle,(uint32_t)index,font_css,(const uint8_t *)"",0));
        } else {
            const char pattern[]="<link href=\"/xjs/"; const char *link=NULL;
            for(size_t i=0;i+sizeof(pattern)-1<=n;++i)
                if(!memcmp(bytes+i,pattern,sizeof(pattern)-1)) { link=(const char *)bytes+i+12; break; }
            assert(link);
            const char *end=strchr(link,'\"'); assert(end);
            static char css_url[BROWSER_RESOURCE_URL_CAPACITY],reference[BROWSER_RESOURCE_URL_CAPACITY];
            assert((size_t)(end-link)<sizeof(reference)); memcpy(reference,link,(size_t)(end-link)); reference[end-link]=0;
            assert(!browser_resource_url(url,reference,css_url));
            f=fopen("build/codex-agent/browser-google.css","rb"); assert(f);
            size_t size=fread(css,1,sizeof(css),f); assert(!ferror(f) && feof(f)); fclose(f);
            assert(browser_resources_add(&resource_bundle,url,css_url,0)==0);
            assert(!browser_resources_store(&resource_bundle,0,css_url,css,(uint32_t)size));
        }
        /* One parse per address space, exactly like production workers. The
         * uncaptured font sheet is empty, not live subresource acceptance. */
        int rc=browser_css_render_document(bytes,n,800,500,NULL,url,&resource_bundle,&resource_needs,&document,&scene,
            argc>=4 ? BROWSER_ENCODING_AUTO : BROWSER_ENCODING_WINDOWS1252);
        printf("PUBLIC_CAPTURE bytes=%u result=%d text=%u runs=%u forms=%u controls=%u strings=%u\n",(unsigned)n,rc,document.text_length,scene.count,scene.forms.form_count,scene.forms.control_count,scene.forms.used);
        assert(!rc && !resource_needs.count && scene.count && document.text_length);
        static uint32_t pixels[800*500]; uint8_t bits[16]; memset(bits,255,sizeof(bits));
        reist_gui_font_mapping_t map={0,REIST_GUI_FONT_EMPTY_GLYPH};
        reist_gui_font_t font={.version=REIST_GUI_FONT_API_VERSION,.struct_size=sizeof(font),
            .data=bits,.data_size=sizeof(bits),.glyph_count=1,.bytes_per_glyph=16,
            .width=8,.height=16,.row_bytes=1,.fallback_glyph=0,.mappings=&map,.mapping_capacity=1};
        for(unsigned i=0;i<3;++i)
            assert(!browser_scene_raster(&document,&scene,&font,NULL,i*scene.total_height/3,pixels,800,500,0,500));
        puts("PUBLIC_CAPTURE_RASTER_OK");
        return rc!=0;
    }
    if(!strcmp(mode,"public-document") || !strcmp(mode,"public-meta")) {
        static uint8_t bytes[90000]; size_t at=0;
        const char prefix[]="<!--"; memcpy(bytes,prefix,4); at=4;
        memset(bytes+at,'x',70000); at+=70000;
        const char html[]="--><meta charset=ISO-8859-1><style>h1{font-size:36px}h2{font-size:64px}</style>"
            "<h1>caf\xe9 \x80</h1><h2>large</h2><p>kept</p>";
        memcpy(bytes+at,html,sizeof(html)-1); at+=sizeof(html)-1;
        int rc=browser_css_render_document(bytes,at,800,500,NULL,"https://example.test/",NULL,NULL,&document,&scene,
            !strcmp(mode,"public-meta") ? BROWSER_ENCODING_AUTO : BROWSER_ENCODING_WINDOWS1252);
        printf("PUBLIC_DOCUMENT result=%d text=%u runs=%u\n",rc,document.text_length,scene.count);
        assert(!rc && text_run("caf\xc3\xa9 \xe2\x82\xac"));
        assert(text_run("caf\xc3\xa9 \xe2\x82\xac")->height==36 && text_run("large")->height==64);
        static uint32_t pixels[800*500],font_header[13];
        font_header[0]=REIST_GUI_FONT_PSF2_MAGIC; font_header[2]=32; font_header[3]=1; font_header[4]=1;
        font_header[5]=16; font_header[6]=16; font_header[7]=8;
        memset((uint8_t *)font_header+32,255,16);
        ((uint8_t *)font_header)[48]='?'; ((uint8_t *)font_header)[49]=255;
        reist_gui_font_t font; reist_gui_font_mapping_t map[128];
        assert(!reist_gui_font_open_psf2(&font,(uint8_t *)font_header,50,map,128,'?'));
        memset(pixels,0xff,sizeof(pixels));
        assert(!browser_scene_raster(&document,&scene,&font,NULL,0,pixels,800,500,0,500));
        unsigned changed=0; for(unsigned i=0;i<800*500;++i) changed+=pixels[i]!=UINT32_MAX;
        assert(changed>1000);
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strcmp(mode,"forms-fixture")) {
        static uint8_t html[65536];
        FILE *fixture=fopen("htdocs/browser-forms-test.html","rb"); assert(fixture);
        size_t length=fread(html,1,sizeof(html),fixture); assert(length && length<sizeof(html) && !ferror(fixture));
        assert(!fclose(fixture));
        assert(!browser_css_render(html,length,782,502,NULL,"/htdocs/browser-forms-test.html",&document,&scene));
        unsigned marker=0;
        for(uint32_t i=0;i<scene.count;++i) {
            const browser_scene_run_t *r=&scene.runs[i];
            if(r->kind==BROWSER_SCENE_FILL && r->color==0xff123456) {
                assert(r->x==12 && r->y==12 && r->width==3 && r->height==3); ++marker;
            }
        }
        assert(marker==1 && scene.forms.form_count==4);
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strcmp(mode,"forms")) {
        const char html[]="<form id=f action='/search'><fieldset disabled><legend><input name=legend value=yes></legend>"
            "<input name=skip value=no></fieldset><label for=q>Query</label><input id=q type=search name=q value='caf&#233;'>"
            "<input type=radio name=r value=a checked><input type=radio name=r value=b checked>"
            "<textarea name=t>\nfirst\nsecond</textarea><select name=s><option disabled>no<option value=a>A<option selected value=b>B</select>"
            "<input type=hidden name=h value=secret><button name=go value=yes>Send</button><button type=reset>Reset</button></form>"
            "<input form=f name=external value=ok><div id=wrong></div><form id=wrong></form><input form=wrong name=orphan>";
        assert(!browser_css_render((const uint8_t *)html,sizeof(html)-1,800,500,NULL,"https://example.test/page",&document,&scene));
        browser_forms_t *m=&scene.forms; static browser_form_state_t values;
        assert(m->form_count==2 && m->control_count==13 && m->option_count==3);
        assert(!(m->controls[0].flags&BROWSER_FORM_DISABLED));
        assert(m->controls[1].flags&BROWSER_FORM_DISABLED);
        assert(m->controls[2].kind==BROWSER_FORM_LABEL && m->controls[2].target==3);
        assert(!strcmp(m->strings+m->controls[3].value,"caf\xc3\xa9"));
        assert(!strcmp(m->strings+m->controls[6].value,"first\nsecond"));
        assert(m->controls[11].owner==0 && m->controls[12].owner==BROWSER_FORM_NONE);
        assert(!browser_forms_bind(m,NULL,&values,7,0));
        assert(!values.checked[4] && values.checked[5] && values.selected[2]);
        char url[256]; assert(!browser_forms_submit(m,&values,7,9,"https://example.test/page",url,sizeof(url)));
        assert(!strcmp(url,"https://example.test/search?legend=yes&q=caf%C3%A9&r=b&t=first%0D%0Asecond&s=b&h=secret&go=yes&external=ok"));
        static browser_html_reply_t reply,decoded; static browser_scene_t copy;
        browser_css_request_t q={.header={BROWSER_HTML_MAGIC,BROWSER_HTML_VERSION,sizeof(q)+sizeof(html)-1,12,77,19,0,0,sizeof(html)-1,0,{0,0}},
            .version=BROWSER_SCENE_VERSION,.width=800,.height=500,.document_url="https://example.test/page"};
        reply.header=q.header; reply.header.size=sizeof(reply); reply.header.child_pid=81; reply.header.child_generation=23; reply.document=document;
        int n=browser_css_pack(&reply,&scene,transport,sizeof(transport)); assert(n>0);
        assert(!browser_css_unpack(transport,(uint32_t)n,&q,81,23,&decoded,&copy));
        assert(!memcmp(&copy,&scene,sizeof(scene)));
        assert(browser_css_unpack(transport,(uint32_t)n,&q,81,24,&decoded,&copy));
        assert(browser_css_unpack(transport,(uint32_t)n-1,&q,81,23,&decoded,&copy));
        m->controls[3].owner=99; assert(browser_scene_validate(&document,&scene));
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strncmp(mode,"bundle-worker",13)) {
        const char body[]="<link rel=stylesheet href='a.css'><p>External</p>";
        browser_css_request_t q={.header={BROWSER_HTML_MAGIC,BROWSER_HTML_VERSION,sizeof(q)+sizeof(body)-1,12,77,19,0,0,sizeof(body)-1,0,{0,0}},
            .version=BROWSER_CSS_RESOURCE_VERSION,.width=800,.height=500,.document_url="https://example.test/page.html"};
        browser_resources_init(&resource_bundle,7);
        unsigned missing=!strcmp(mode,"bundle-worker-needs");
        if(!missing) {
            const char style[]="p {color:#123456}";
            assert(browser_resources_add(&resource_bundle,q.document_url,"https://example.test/a.css",0)==0);
            assert(!browser_resources_store(&resource_bundle,0,"https://example.test/a.css",(const uint8_t *)style,sizeof(style)-1));
        }
        int packed=browser_resources_pack(&resource_bundle,q.document_url,request_wire+q.header.size,sizeof(request_wire)-q.header.size);
        assert(packed>0); q.header.size+=(uint32_t)packed;
        memcpy(request_wire,&q,sizeof(q)); memcpy(request_wire+sizeof(q),body,sizeof(body)-1); request_length=q.header.size;
        if(!strcmp(mode,"bundle-worker-offset")) {
            uint32_t invalid=UINT32_MAX;
            memcpy(request_wire+sizeof(q)+sizeof(body)-1+offsetof(browser_resources_t,entries),&invalid,sizeof(invalid));
        }
        private_fail=!strcmp(mode,"bundle-worker-oom");
        char *args[]={"htmlwork","--ipc","42"}; int rc=html_worker_main(3,args);
        assert(transfer_allocs==1 && transfer_frees==1 && !transfer_live);
        if(!strcmp(mode,"bundle-worker-offset")) assert(rc==74 && !received && !private_used);
        else if(private_fail) assert(rc==65 && !received && !private_used);
        else if(missing) {
            static browser_resource_needs_t n;
            assert(!rc && received==transport_length && received<=sizeof(n));
            memcpy(&n,transport,received);
            assert(!browser_resource_needs_validate(&n,received,&q.header,81,23,&resource_bundle,q.document_url));
            assert(n.count==1 && n.items[0].depth==0 && !strcmp(n.items[0].url,"https://example.test/a.css"));
            assert(browser_resource_needs_validate(&n,received,&q.header,81,24,&resource_bundle,q.document_url));
        } else {
            static browser_html_reply_t parsed;
            assert(!rc && private_used && received==transport_length);
            assert(!browser_css_unpack(transport,received,&q,81,23,&parsed,&scene));
            document=parsed.document; const browser_scene_run_t *r=text_run("External");
            assert(r && r->color==0xff123456);
        }
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strncmp(mode,"chain-",6)) {
        const char *url="https://example.test/page.html";
        browser_resources_init(&resource_bundle,7);
        for(unsigned i=0;i<=8;++i) {
            char sheet_url[256],bytes[128]; snprintf(sheet_url,sizeof(sheet_url),"https://example.test/%u.css",i);
            if(i<8 || !strcmp(mode,"chain-overflow")) snprintf(bytes,sizeof(bytes),"@import '%u.css';",i+1);
            else memcpy(bytes,"p {color:#123456}",sizeof("p {color:#123456}"));
            assert(browser_resources_add(&resource_bundle,url,sheet_url,i)==(int)i);
            assert(!browser_resources_store(&resource_bundle,i,sheet_url,(const uint8_t *)bytes,(uint32_t)strlen(bytes)));
        }
        const char body[]="<link rel=stylesheet href='0.css'><p>External</p>";
        int rc=browser_css_render_resources((const uint8_t *)body,sizeof(body)-1,800,500,NULL,url,
            &resource_bundle,&resource_needs,&document,&scene);
        if(!strcmp(mode,"chain-overflow")) assert(rc<0);
        else { const browser_scene_run_t *r=text_run("External"); assert(!rc && r && r->color==0xff123456); }
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if(!strncmp(mode,"resources-",10)) {
        const char *url="https://example.test/document.html";
        const char body[]="<style>p {color:#111111}</style><link rel=stylesheet href='a.css'>"
            "<link rel=stylesheet href='a.css'><link rel='alternate stylesheet' href='unused.css'>"
            "<link rel=stylesheet media=print href='print.css'><p>External</p>";
        browser_resources_init(&resource_bundle,7);
        if(strcmp(mode,"resources-needed")) {
            const char *a=!strcmp(mode,"resources-cycle") ? "@import 'a.css'; p {color:#123456}" : "@import 'b.css'; p {font-size:20px}";
            assert(browser_resources_add(&resource_bundle,url,"https://example.test/a.css",1)==0);
            assert(!browser_resources_store(&resource_bundle,0,"https://example.test/a.css",(const uint8_t *)a,(uint32_t)strlen(a)));
            const char b[]="p {color:#123456 !important}";
            assert(browser_resources_add(&resource_bundle,url,"https://example.test/b.css",2)==1);
            assert(!browser_resources_store(&resource_bundle,1,"https://example.test/b.css",(const uint8_t *)b,sizeof(b)-1));
            const char print[]="p {color:red !important}";
            assert(browser_resources_add(&resource_bundle,url,"https://example.test/print.css",1)==2);
            assert(!browser_resources_store(&resource_bundle,2,"https://example.test/print.css",(const uint8_t *)print,sizeof(print)-1));
        }
        if(!strcmp(mode,"resources-depth")) {
            resource_bundle.entries[0].depth=9;
            assert(browser_css_render_resources((const uint8_t *)body,sizeof(body)-1,800,500,NULL,url,
                &resource_bundle,&resource_needs,&document,&scene)<0);
        } else {
            int rc=browser_css_render_resources((const uint8_t *)body,sizeof(body)-1,800,500,NULL,url,
                &resource_bundle,&resource_needs,&document,&scene);
            if(!strcmp(mode,"resources-needed")) {
                assert(rc==1 && resource_needs.count==2);
                assert(!strcmp(resource_needs.items[0].url,"https://example.test/a.css"));
            } else {
                assert(rc==0 && !resource_needs.count);
                const browser_scene_run_t *r=text_run("External");
                assert(r && r->color==0xff123456);
                if(!strcmp(mode,"resources-cascade")) assert(r->height==20);
            }
        }
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    const char html[]="<title>CSS fixture</title><style>"
        "div {color: red; width:200px; padding:10px; border:2px solid #112233; margin:8px}"
        ".note {color:blue} #box {color:#008000} #box p > span {color:#123456 !important}"
        ".hidden {display:none} p {margin:0; text-align:center; font-size:20px}"
        "</style><div id=box class=note>Inherited<p><span style='color:red'>Priority</span></p>"
        "<b>Bold</b><i>Italic</i><a href='next.html'>Link</a></div><p class=hidden>SECRET</p>";
    if (!strncmp(mode,"worker",6) || !strcmp(mode,"bad-packet")) {
        browser_css_request_t q={.header={BROWSER_HTML_MAGIC,BROWSER_HTML_VERSION,sizeof(q)+sizeof(html)-1,12,77,19,0,0,sizeof(html)-1,0,{0,0}},
            .version=1,.width=800,.height=500,.document_url="https://example.test/document.html"};
        memcpy(request_wire,&q,sizeof(q)); memcpy(request_wire+sizeof(q),html,sizeof(html)-1); request_length=q.header.size;
        bad_packet=!strcmp(mode,"bad-packet");
        if (!strcmp(mode,"worker-eacces") || !strcmp(mode,"worker-missing-eacces") ||
            !strcmp(mode,"worker-revoked-eacces")) startup_error=-13;
        no_delegation=!strncmp(mode,"worker-missing",14);
        revoke_after_packet=!strncmp(mode,"worker-revoked",14);
        sleep_failure=!strcmp(mode,"worker-sleep-failure");
        transfer_fail=!strcmp(mode,"worker-buffer-oom");
        transfer_delay=!strcmp(mode,"worker-buffer-deadline") ? 5000 : 0;
        char *args[]={"htmlwork","--ipc","42"}; int rc=html_worker_main(3,args);
        assert(transfer_allocs==1 && transfer_frees==!transfer_fail && !transfer_live);
        if(transfer_fail || transfer_delay) {
            assert(rc==(transfer_fail ? 71 : 74) && !sent && !received && !sleeps);
        } else if (bad_packet || no_delegation || revoke_after_packet || sleep_failure) {
            assert(sleeps);
            assert(rc==74 && !received);
            if (no_delegation) assert(!sent && time_ms>=5000 && time_ms<=5002);
            if (revoke_after_packet) assert(sent==53 && sleeps==1 && time_ms<10);
            if (sleep_failure) assert(!sent && sleeps==1 && time_ms<10);
        }
        else {
            assert(sleeps);
            assert(!rc && received==transport_length && received>0);
            static browser_html_reply_t parsed;
            assert(!browser_css_unpack(transport,transport_length,&q,81,23,&parsed,&scene));
            assert(browser_css_unpack(transport,transport_length,&q,81,24,&parsed,&scene)==-84);
            ++q.width; assert(browser_css_unpack(transport,transport_length,&q,81,23,&parsed,&scene)==-84);
        }
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if (!strcmp(mode,"geometry")) {
        const char body[]="<style>body {margin:0} #box {width:50%;margin:10px auto;padding:1em;border:solid #112233;"
            "background:#abcdef;font-size:16px} #box p {margin:0;height:40px;color:#123456}"
            "span {font-size:150%} .gone {display:none}</style><div id=box><p><span>Size</span></p></div><p class=gone>SECRET</p>";
        assert(!browser_css_render((const uint8_t *)body,sizeof(body)-1,800,500,NULL,NULL,&document,&scene));
        const browser_scene_run_t *r=text_run("Size");
        /* LibCSS resolves the CSS medium border keyword to two CSS pixels. */
        assert(r && r->height==24 && r->x==200 && r->y==28 && r->color==0xff123456);
        assert(scene.total_height>=88 && !text_run("SECRET"));
        static uint32_t pixels[800*500], font_header[13];
        font_header[0]=REIST_GUI_FONT_PSF2_MAGIC; font_header[2]=32; font_header[3]=1; font_header[4]=1;
        font_header[5]=16; font_header[6]=16; font_header[7]=8;
        memset((uint8_t *)font_header+32,255,16);
        ((uint8_t *)font_header)[48]='?'; ((uint8_t *)font_header)[49]=255;
        reist_gui_font_t font; reist_gui_font_mapping_t map[128];
        assert(!reist_gui_font_open_psf2(&font,(uint8_t *)font_header,50,map,128,'?'));
        for(unsigned i=0;i<800*500;++i) pixels[i]=0xffffff;
        assert(!browser_scene_raster(&document,&scene,&font,NULL,0,pixels,800,500,0,500));
        assert(pixels[10*800+182]==0x112233 && pixels[15*800+190]==0xabcdef && pixels[30*800+201]==0x123456);
        /* Every scroll/resize clips text and decorations, including negative y. */
        for(unsigned y=0;y<100;++y) assert(!browser_scene_raster(&document,&scene,&font,NULL,y,pixels,320,200,20,100));
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if (!strcmp(mode,"quota")) {
        const char huge[]="<p style='font-size:100000px'>reject</p>";
        assert(browser_css_render((const uint8_t *)huge,sizeof(huge)-1,800,500,NULL,NULL,&document,&scene)!=0);
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if (!strcmp(mode,"resources")) {
        const char body[]="<link rel=stylesheet href='https://example.test/x.css'><style>"
            "p {unknown-property:garbage;color:#123456;background-image:url(../secret.png)}"
            "</style><p>Inert</p><script>NETWORK_MUST_NOT_RUN</script>";
        assert(!browser_css_render((const uint8_t *)body,sizeof(body)-1,800,500,NULL,"https://example.test/a/document.html",&document,&scene));
        const browser_scene_run_t *r=text_run("Inert");
        assert(r && r->color==0xff123456 && !text_run("NETWORK_MUST_NOT_RUN"));
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    if (!strcmp(mode,"import")) {
        const char imported[]="<style>@import url(https://example.test/a.css); p {color:red}</style><p>unfetched</p>";
        assert(browser_css_render((const uint8_t *)imported,sizeof(imported)-1,800,500,NULL,NULL,&document,&scene)!=0);
        puts("CSS_CASCADE_SCENE_OK"); return 0;
    }
    assert(!browser_css_render((const uint8_t *)html,sizeof(html)-1,800,500,NULL,NULL,&document,&scene));
    assert(!browser_scene_validate(&document,&scene));
    assert(!strcmp(document.title,"CSS fixture"));
    const browser_scene_run_t *inherited=text_run("Inherited"), *priority=text_run("Priority");
    assert(inherited && inherited->color==0xff008000U);
    assert(priority && priority->color==0xff123456U && priority->height==20);
    assert(priority->x>inherited->x && priority->y>inherited->y);
    assert(text_run("Bold") && (text_run("Bold")->flags&1));
    assert(text_run("Italic") && (text_run("Italic")->flags&2));
    assert(!text_run("SECRET") && document.link_count==1);
    assert(!strcmp(document.links[0].href,"next.html"));
    uint32_t saved=scene.runs[0].kind; scene.runs[0].kind=99;
    assert(browser_scene_validate(&document,&scene)==-84); scene.runs[0].kind=saved;
    browser_scene_run_t original=scene.runs[scene.count-1];
    scene.runs[scene.count-1].x=INT32_MAX; assert(browser_scene_validate(&document,&scene)==-84);
    scene.runs[scene.count-1]=original;
    scene.runs[scene.count-1].offset=UINT32_MAX; assert(browser_scene_validate(&document,&scene)==-84);
    scene.runs[scene.count-1]=original;
    browser_css_packet_t p={BROWSER_CSS_PACKET_MAGIC,12,0,4,{1,2,3,4}};
    uint8_t target[4]={0}; uint32_t offset=0,total=0;
    assert(browser_css_packet_accept(&p,20,13,target,4,&offset,&total)==-84 && !offset && !total && !target[0]);
    assert(!browser_css_packet_accept(&p,20,12,target,4,&offset,&total) && offset==4 && target[3]==4);
    assert(browser_css_packet_accept(&p,20,12,target,4,&offset,&total)==-84);
    puts("CSS_CASCADE_SCENE_OK"); return 0;
}
