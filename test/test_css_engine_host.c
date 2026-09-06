#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "userspace/gui/apps/browser/css_engine.h"
#define REIST_CSS_WORKER
#define main static html_worker_main
#include "userspace/gui/apps/browser/html_worker.c"
#undef main
extern _Noreturn void _Exit(int);
#undef assert
#define assert(expr) do { if (!(expr)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); _Exit(1); } } while (0)
static reist_html_document_t document;
static browser_scene_t scene;
static uint8_t transport[BROWSER_CSS_WIRE_CAPACITY], request_wire[sizeof(browser_css_request_t)+65536];
static uint32_t sent, received, request_length, transport_length, time_ms, bad_packet;
static uint32_t sleeps, no_delegation, revoke_after_packet, sleep_failure;
static int startup_error=-9;
int x86os_getpid(void) { return 81; }
uint32_t x86os_uptime_ms(void) { return time_ms++; }
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
    const char *mode=argc==2 ? argv[1] : "cascade";
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
        char *args[]={"htmlwork","--ipc","42"}; int rc=html_worker_main(3,args);
        assert(sleeps);
        if (bad_packet || no_delegation || revoke_after_packet || sleep_failure) {
            assert(rc==74 && !received);
            if (no_delegation) assert(!sent && time_ms>=5000 && time_ms<=5002);
            if (revoke_after_packet) assert(sent==53 && sleeps==1 && time_ms<10);
            if (sleep_failure) assert(!sent && sleeps==1 && time_ms<10);
        }
        else {
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
