#include "userspace/gui/apps/browser/font_engine.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(v) do {if(!(v)) {fprintf(stderr,"font:%d %s\n",__LINE__,#v);exit(1);}} while(0)
static browser_font_atlas atlas;
static uint32_t be32(const unsigned char *p) {return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
int main(int argc,char **argv) {
    CHECK(argc==3);const char *mode=argv[1];
    CHECK(!browser_font_begin(&atlas,nullptr));
    if(!strcmp(mode,"glyphs") || !strcmp(mode,"sizes")) {
        for(unsigned face=1;face<=8;++face) for(unsigned height=12;height<=64;height+=13) {
            const browser_font_glyph *thin=nullptr,*wide=nullptr;
            CHECK(!browser_font_get(face,height,'i',&thin));int a=thin->advance;
            CHECK(!browser_font_get(face,height,'W',&wide));CHECK(wide->advance>a);
            CHECK(wide->width && wide->height && wide->width<=96 && wide->height<=96);
        }
        bool gray=false;for(unsigned i=0;i<atlas.used;++i) if(atlas.alpha[i] && atlas.alpha[i]!=255) gray=true;CHECK(gray);
    } else if(!strcmp(mode,"broken")) {
        unsigned char broken[64]={};CHECK(browser_font_check(broken,sizeof(broken))<0);
        FILE *f=fopen(argv[2],"rb");CHECK(f);CHECK(fread(broken,1,64,f)==64);fclose(f);
        CHECK(browser_font_check(broken,sizeof(broken))<0);
        static unsigned char original[1024*1024];
        f=fopen(argv[2],"rb");CHECK(f);size_t bytes=fread(original,1,sizeof(original),f);fclose(f);
        CHECK(bytes>64 && bytes<sizeof(original));CHECK(!browser_font_check(original,bytes));
        /* Genuine sfnt directory with a corrupt cmap body, not just a bad magic. */
        unsigned tables=((unsigned)original[4]<<8)|original[5];bool found=false;
        CHECK(tables<128 && 12+tables*16<bytes);
        for(unsigned i=0;i<tables;++i) {
            unsigned char *entry=original+12+i*16;
            if(memcmp(entry,"cmap",4))continue;
            uint32_t offset=be32(entry+8),length=be32(entry+12);
            CHECK(offset<bytes && length<=bytes-offset);memset(original+offset,0xff,length);found=true;break;
        }
        CHECK(found && browser_font_check(original,bytes)<0);
    } else if(!strcmp(mode,"quota")) {
        for(unsigned allowance=0;allowance<96;++allowance) {
            browser_font_finish();CHECK(!browser_font_live_bytes());CHECK(!browser_font_begin(&atlas,nullptr));
            browser_font_fail_after(allowance);
            const browser_font_glyph *g=nullptr;int rc=browser_font_get(1,16,'A',&g);
            CHECK(rc<0 || (rc==0 && g));
            if(!allowance) CHECK(rc<0 && !g && !atlas.count);
        }
        browser_font_finish();CHECK(!browser_font_live_bytes());CHECK(!browser_font_begin(&atlas,nullptr));
        bool exhausted=false;
        for(unsigned face=1;face<=8 && !exhausted;++face) for(unsigned c=33;c<127;++c) {
            const browser_font_glyph *g=nullptr;int rc=browser_font_get(face,64,c,&g);
            if(rc<0) {exhausted=true;break;}CHECK(!rc && g);
        }
        CHECK(exhausted && atlas.count<=BROWSER_FONT_GLYPHS && atlas.used<=BROWSER_FONT_ALPHA);
    } else if(!strcmp(mode,"cache")) {
        const browser_font_glyph *g=nullptr;CHECK(!browser_font_get(1,32,'A',&g));
        unsigned count=atlas.count,bytes=atlas.used;
        for(unsigned i=0;i<256;++i) CHECK(!browser_font_get(1,32,'A',&g));
        CHECK(atlas.count==count && atlas.used==bytes && browser_font_rasterizations()==1);
        CHECK(browser_font_get(9,32,'A',&g)<0 && browser_font_get(1,65,'A',&g)<0);
    } else CHECK(false);
    browser_font_finish();CHECK(browser_font_live_bytes()==0);puts("BROWSER_TTF_OK");
}
