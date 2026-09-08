#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern "C" {
#include "userspace/gui/apps/browser/css_engine.h"
}
extern "C" [[noreturn]] void _Exit(int);
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"layout:%d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
static reist_html_document_t doc;
static browser_scene_t scene;
static void render(const char *html, uint32_t width=800) {
    int result=browser_css_render_document((const uint8_t *)html,strlen(html),width,600,nullptr,
        "/htdocs/layout.htm",nullptr,nullptr,&doc,&scene,0);
    if(result) fprintf(stderr,"render error %d, runs %u, text %u, height %u\n",result,scene.count,doc.text_length,scene.total_height);
    CHECK(!result); CHECK(!browser_scene_validate(&doc,&scene));
}
static const browser_scene_run_t &text(const char *s) {
    for(uint32_t i=0;i<scene.count;++i) {
        auto &r=scene.runs[i];
        if(r.kind==1 && r.length==strlen(s) && !memcmp(doc.text+r.offset,s,r.length)) return r;
    }
    fprintf(stderr,"missing text %s\n",s); _Exit(1);
}
static size_t read_file(const char *name,char *out,size_t capacity) {
    FILE *f=fopen(name,"rb"); CHECK(f);
    size_t n=fread(out,1,capacity-1,f); CHECK(!ferror(f) && n<capacity-1); fclose(f); out[n]=0; return n;
}
static const char *find_text(const char *input,size_t size,const char *needle) {
    size_t n=strlen(needle);
    for(size_t i=0;i+n<=size;++i) if(!memcmp(input+i,needle,n)) return input+i;
    return nullptr;
}
extern "C" int browser_layout_host_main(const char *mode) {
    if(!strcmp(mode,"layout-variables")) {
        render("<style>:root{--Ink:#123456;--ink:#abcdef;--size:80px}body{margin:0}"
            "p{margin:0;color:var(--Ink);width:var(--size);background:var(--ink)}"
            "span{color:var(--missing,var(--Ink))}</style><p><span>VALUE</span></p>");
        CHECK(text("VALUE").color==0xff123456);
        bool box=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].kind==7 && scene.runs[i].color==0xffabcdef) {
            CHECK(scene.runs[i].width==80); box=true;
        } CHECK(box);
    } else if(!strcmp(mode,"layout-cascade")) {
        render("<style>body{--x:#112233;color:#010203}.a{--x:#334455!important}"
            "#p{--x:#aabbcc}p{--x:#ffffff;color:var(--x)}"
            "p{color:red;color:var(--bad)}b{color:var(--x)}</style>"
            "<p id=p class=a style='--x:#000000'><b>IMPORTANT</b><span>INVALID</span></p>");
        CHECK(text("IMPORTANT").color==0xff334455);
        CHECK(text("INVALID").color==0xff010203);
    } else if(!strcmp(mode,"layout-cycles")) {
        render("<style>:root{--a:var(--b);--b:var(--a);--c:var(--a,#123456);--d:var(--c,var(--d))}"
            "p{color:var(--c)}b{color:var(--d,#abcdef)}</style><p>CYCLE</p><b>FALLBACK</b>");
        CHECK(text("CYCLE").color==0xff123456); CHECK(text("FALLBACK").color==0xffabcdef);
    } else if(!strcmp(mode,"layout-tokens")) {
        render("<style>:root{--n:20;--x:#123456;--q:'var(--absent)'}"
            "body{color:#010203}p{color:var(--x);width:var(--n)px;background:#ffeedd}"
            "b{color:var(--missing, rgb(10,20,30))}</style><p>TOKENS</p><b>RGB</b>");
        CHECK(text("TOKENS").color==0xff123456); CHECK(text("RGB").color==0xff0a141e);
        bool full=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].color==0xffffeedd) { CHECK(scene.runs[i].width==768); full=true; } CHECK(full);
    } else if(!strcmp(mode,"layout-inheritance")) {
        render("<style>body,div{margin:0}.parent{--base:50px;--frozen:var(--base);--color:#123456}"
            ".child{--base:120px;--color:initial;width:var(--frozen);background:var(--color,#abcdef)}"
            ".child span{color:var(--\\63 olor,#334455)}</style><div class=parent><div class=child><span>FROZE</span></div></div>");
        CHECK(text("FROZE").color==0xff334455);
        bool frozen=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].color==0xffabcdef) { CHECK(scene.runs[i].width==50); frozen=true; } CHECK(frozen);
    } else if(!strcmp(mode,"layout-malformed")) {
        render("<style>:root{--empty:}body{color:#112233}p{color:red;color:var(--empty,blue)}"
            "b{color:#445566;color:var(not-custom)}body,div{margin:0}"
            ".g{display:grid;width:100px;grid-template-columns:20px 1fr;grid-template-columns:minmax(20px,80px) 1fr}"
            ".r{width:40px;height:40px;background:#abcdef;border-radius:6px;border-radius:50%}</style>"
            "<p>EMPTY</p><b>GRAMMAR</b><div class=g><div>A</div><div>B</div></div><div class=r></div>");
        CHECK(text("EMPTY").color==0xff112233 && text("GRAMMAR").color==0xff445566);
        CHECK(text("B").x==20);
        bool round=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].kind==BROWSER_SCENE_ROUND && scene.runs[i].color==0xffabcdef) { CHECK(scene.runs[i].offset==6); round=true; } CHECK(round);
    } else if(!strcmp(mode,"layout-boxes")) {
        render("<style>body,div{margin:0}.box{width:100%;max-width:400px;min-height:80px;"
            "box-sizing:border-box;padding:10px;border:2px solid;margin:auto;background:#abcdef}"
            "p{margin:0}</style><div class=box><p>BOX</p></div>");
        CHECK(text("BOX").x==212 && text("BOX").y==12);
        bool box=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].kind==7 && scene.runs[i].color==0xffabcdef) {
            CHECK(scene.runs[i].width==400 && scene.runs[i].height==80); box=true;
        } CHECK(box);
    } else if(!strcmp(mode,"layout-flex")) {
        render("<style>body,div{margin:0}.row{display:flex;gap:10px;width:400px}"
            ".row div{flex:1;min-width:0;background:#abcdef;height:40px}"
            ".row div+div{flex:3}</style><div class=row><div>ONE</div><div>TWO</div></div>");
        CHECK(text("ONE").x==0 && text("TWO").x==107 && text("ONE").y==text("TWO").y);
    } else if(!strcmp(mode,"layout-flex-wrap")) {
        render("<style>body,div{margin:0}.row{display:flex;flex-wrap:wrap;gap:10px;width:250px}"
            ".row div{width:110px;height:30px;flex-shrink:0}</style><div class=row><div>A</div><div>B</div><div>C</div></div>");
        CHECK(text("A").x==0 && text("B").x==120 && text("C").x==0 && text("C").y==40);
    } else if(!strcmp(mode,"layout-flex-column")) {
        render("<style>body,div{margin:0}.row{display:flex;flex-direction:column;gap:10px;height:100px;align-items:center}"
            ".row div{width:80px;height:30px}</style><div class=row><div>COLA</div><div>COLB</div></div>");
        CHECK(text("COLA").x==360 && text("COLB").y==40);
    } else if(!strcmp(mode,"layout-flex-align")) {
        render("<style>body,div{margin:0}.row{display:flex;width:400px;height:80px;justify-content:space-between;align-items:center}"
            "a{display:inline-flex;width:100px;box-sizing:border-box;padding:10px;justify-content:center}"
            ".last{width:80px;height:20px;align-self:flex-end}</style><div class=row><a>A</a><div class=last>B</div></div>");
        CHECK(text("A").x==46 && text("A").y==31 && text("B").x==320 && text("B").y==60);
    } else if(!strcmp(mode,"layout-flex-reverse")) {
        render("<style>body,div{margin:0}.row{display:flex;width:300px;flex-direction:row-reverse;gap:10px;justify-content:center}"
            ".row div{width:80px;height:20px}</style><div class=row><div>A</div><div>B</div></div>");
        CHECK(text("A").x==155 && text("B").x==65);
    } else if(!strcmp(mode,"layout-flex-shrink")) {
        render("<style>body,div{margin:0}.row{display:flex;width:240px}.a{width:200px;min-width:180px}.b{width:100px;min-width:0}</style>"
            "<div class=row><div class=a>A</div><div class=b>B</div></div>");
        CHECK(text("B").x==180);
    } else if(!strcmp(mode,"layout-grid")) {
        render("<style>body,div{margin:0}.grid{display:grid;width:600px;grid-template-columns:100px 1fr 2fr;gap:12px}"
            ".grid div{height:30px}</style><div class=grid><div>G1</div><div>G2</div><div>G3</div><div>G4</div></div>");
        CHECK(text("G1").x==0 && text("G2").x==112 && text("G3").x==282 && text("G4").y==42);
    } else if(!strcmp(mode,"layout-grid-wide") || !strcmp(mode,"layout-grid-narrow")) {
        const char *fit="<style>body,div{margin:0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px}"
            ".grid div{height:30px}</style><div class=grid><div>FIT1</div><div>FIT2</div><div>FIT3</div></div>";
        bool wide=!strcmp(mode,"layout-grid-wide"); render(fit,wide ? 700 : 430);
        CHECK(text("FIT2").x==(wide ? 236 : 220) && text("FIT3").y==(wide ? 0 : 40));
    } else if(!strcmp(mode,"layout-grid-minimum")) {
        render("<style>body,div{margin:0}.grid{display:grid;width:600px;grid-template-columns:minmax(500px,1fr) minmax(75px,1fr)}"
            ".grid div{background:#abcdef}</style><div class=grid><div>A</div><div>B</div></div>");
        CHECK(text("B").x==500);
        bool size=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].color==0xffabcdef && scene.runs[i].x==500) { CHECK(scene.runs[i].width==100); size=true; } CHECK(size);
    } else if(!strcmp(mode,"layout-nested")) {
        render("<style>body,div{margin:0}.row{display:flex;width:700px;gap:20px}.grid{display:grid;flex:1;min-width:0;grid-template-columns:repeat(2,1fr);gap:20px}"
            ".last{width:200px}.inner{display:flex;gap:4px}</style><div class=row><div class=grid><div class=inner><span>A</span><span>B</span></div><div>C</div></div><div class=last>D</div></div>");
        CHECK(text("A").x==0 && text("B").x==12 && text("C").x==250 && text("D").x==500);
    } else if(!strcmp(mode,"layout-expansion")) {
        char input[16384]; size_t used=(size_t)sprintf(input,"<style>:root{--v0:one;");
        for(unsigned i=1;i<=10;++i) used+=(size_t)sprintf(input+used,"--v%u:var(--v%u) var(--v%u);",i,i-1,i-1);
        used+=(size_t)sprintf(input+used,"}</style><p>Expansion quota</p>");
        CHECK(browser_css_render_document((const uint8_t *)input,used,800,600,nullptr,"/layout",nullptr,nullptr,&doc,&scene,0)==-28);
    } else if(!strcmp(mode,"layout-overflow")) {
        const char *input="<style>div{width:262140px;padding:8px}</style><div>Overflow</div>";
        CHECK(browser_css_render_document((const uint8_t *)input,strlen(input),800,600,nullptr,"/layout",nullptr,nullptr,&doc,&scene,0)==-28);
    } else if(!strcmp(mode,"layout-limits")) {
        char input[16384]; size_t used=0;
        used+=(size_t)sprintf(input+used,"<style>:root{");
        for(unsigned i=0;i<65;++i) used+=(size_t)sprintf(input+used,"--v%u:%upx;",i,i);
        used+=(size_t)sprintf(input+used,"}</style><p>quota</p>");
        CHECK(browser_css_render_document((const uint8_t *)input,used,800,600,nullptr,"/layout",nullptr,nullptr,&doc,&scene,0)==-28);
    } else if(!strcmp(mode,"layout-decoration")) {
        render("<style>body{margin:0}a{display:inline-flex;width:100px;height:80px;background:#123456;"
            "border:2px solid #000000;border-radius:12px}"
            ".helper{position:absolute;clip:rect(0,0,0,0)}</style><a href='#end'></a><p class=helper>HIDDEN</p>");
        CHECK(scene.version==BROWSER_SCENE_LAYOUT_VERSION && doc.text_length==0 && doc.link_count==1);
        scene.version=BROWSER_SCENE_VERSION; CHECK(browser_scene_validate(&doc,&scene)==-84);
        scene.version=BROWSER_SCENE_DOCUMENT_VERSION; CHECK(browser_scene_validate(&doc,&scene)==-84);
        scene.version=BROWSER_SCENE_LAYOUT_VERSION;
        static uint32_t pixels[800*600]; for(auto &p:pixels) p=0xffffff;
        CHECK(!browser_scene_raster(&doc,&scene,nullptr,nullptr,0,pixels,800,600,0,600));
        CHECK(pixels[0]==0xffffff && pixels[40*800]==0 && pixels[40*800+40]==0x123456);
        bool hit=false; for(uint32_t i=0;i<scene.count;++i) if(scene.runs[i].kind==BROWSER_SCENE_ROUND && scene.runs[i].link==0) hit=true; CHECK(hit);
        scene.runs[scene.count-1].offset=65;
        CHECK(browser_scene_validate(&doc,&scene)==-84);
        uint32_t before=pixels[0]; CHECK(browser_scene_raster(&doc,&scene,nullptr,nullptr,0,pixels,800,600,0,600)==-84 && pixels[0]==before);
    } else if(!strcmp(mode,"layout-shadow")) {
        render("<style>body{margin:0}div{margin:20px;width:80px;height:60px;border-radius:12px;box-shadow:0 4px 8px rgba(0,0,0,0.2)}</style><div></div>");
        static uint32_t pixels[800*600]; for(auto &p:pixels) p=0xffffff;
        CHECK(scene.version==BROWSER_SCENE_LAYOUT_VERSION);
        CHECK(!browser_scene_raster(&doc,&scene,nullptr,nullptr,0,pixels,800,600,0,600));
        CHECK(pixels[0]==0xffffff && pixels[40*800+40]==0xcccccc);
    } else if(!strcmp(mode,"layout-fixture") || !strcmp(mode,"layout-fixture-narrow")) {
        static char html[16384],css[16384],joined[32768];
        size_t h=read_file("htdocs/layout.htm",html,sizeof(html)),c=read_file("htdocs/layout.css",css,sizeof(css));
        const char *link=find_text(html,h,"<link rel=\"stylesheet\""); CHECK(link);
        const char *after=strchr(link,'>'); CHECK(after); ++after;
        size_t prefix=(size_t)(link-html),at=0;
        memcpy(joined,html,prefix); at=prefix; memcpy(joined+at,"<style>",7); at+=7;
        memcpy(joined+at,css,c); at+=c; memcpy(joined+at,"</style>",8); at+=8;
        memcpy(joined+at,after,h-(size_t)(after-html)+1);
        bool narrow=!strcmp(mode,"layout-fixture-narrow"); render(joined,narrow ? 480 : 800);
        const auto &a=text("Mathematics"),&b=text("Computation");
        CHECK(narrow ? b.y>a.y && b.x==a.x : b.x>a.x && b.y==a.y);
        CHECK(scene.version==BROWSER_SCENE_LAYOUT_VERSION && scene.total_height>600);
        CHECK(!find_text(doc.text,doc.text_length,"Hidden helper"));
    } else {
        /* Added geometry/fault vectors must be implemented, never silently pass. */
        fprintf(stderr,"missing layout vector: %s\n",mode); return 1;
    }
    puts("BROWSER_LAYOUT_OK"); return 0;
}
