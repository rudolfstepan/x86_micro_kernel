#include "font_engine.hpp"
#include <stdlib.h>
#include <string.h>
#include "font_stdlib.h"
#ifndef FT_CONFIG_OPTIONS_H
#define FT_CONFIG_OPTIONS_H "reist_ftoption.h"
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <freetype/internal/ftdrv.h>
#include FT_RENDER_H
extern "C" {
extern const FT_Driver_ClassRec tt_driver_class;
extern const FT_Module_Class sfnt_module_class;
extern const FT_Renderer_Class ft_smooth_renderer_class;
#define FACE(f,s) extern const uint8_t reist_ttf_##f##_##s[],reist_ttf_##f##_##s##_end[];
FACE(Serif,Regular) FACE(Serif,Bold) FACE(Serif,Italic) FACE(Serif,BoldItalic)
FACE(Sans,Regular) FACE(Sans,Bold) FACE(Sans,Italic) FACE(Sans,BoldItalic)
}
namespace {
constexpr uint32_t MemoryLimit=4U*1024U*1024U;
struct Allocation { size_t bytes; max_align_t alignment; };
uint32_t live,allocations_left=UINT32_MAX,rasterizations;
int (*charge)();
bool failed;
browser_font_atlas *atlas;
void *allocate(FT_Memory,long amount) {
    if(amount<=0 || MemoryLimit-live<sizeof(Allocation) || (unsigned long)amount>MemoryLimit-live-sizeof(Allocation) ||
       !allocations_left || (charge && charge())) { failed=true;return nullptr; }
    auto *p=static_cast<Allocation *>(malloc(sizeof(Allocation)+(size_t)amount));
    if(!p) { failed=true;return nullptr; }
    p->bytes=(size_t)amount;live+=(uint32_t)amount+(uint32_t)sizeof(Allocation);--allocations_left;return p+1;
}
void release(FT_Memory,void *pointer) {
    if(!pointer)return;
    auto *p=static_cast<Allocation *>(pointer)-1;live-=(uint32_t)p->bytes+(uint32_t)sizeof(Allocation);free(p);
}
void *resize(FT_Memory m,long old_size,long size,void *pointer) {
    (void)old_size;if(size<=0) { release(m,pointer);return nullptr; }
    void *fresh=allocate(m,size);if(!fresh)return nullptr;
    if(pointer) { auto *p=static_cast<Allocation *>(pointer)-1;memcpy(fresh,pointer,p->bytes<(size_t)size ? p->bytes : (size_t)size);release(m,pointer); }
    return fresh;
}
FT_MemoryRec_ memory={nullptr,allocate,release,resize};
class FontLibrary {
public:
    FT_Library library=nullptr;
    FT_Face faces[8]={};
    FontLibrary()=default;
    FontLibrary(const FontLibrary &)=delete;
    FontLibrary &operator=(const FontLibrary &)=delete;
    void clear() {
        for(auto &face:faces) if(face) { FT_Done_Face(face);face=nullptr; }
        if(library) { FT_Done_Library(library);library=nullptr; }
    }
    int open() {
        if(library)return 0;
        if(FT_New_Library(&memory,&library))return -28;
        if(FT_Add_Module(library,&sfnt_module_class) || FT_Add_Module(library,&tt_driver_class.root) ||
           FT_Add_Module(library,&ft_smooth_renderer_class.root)) { clear();return -28; }
        return 0;
    }
};
/* No global destructor registration: explicit generation cleanup below. */
FontLibrary fonts;
struct Source { const uint8_t *start,*end; };
#define SOURCE(f,s) {reist_ttf_##f##_##s,reist_ttf_##f##_##s##_end}
const Source sources[]={SOURCE(Serif,Regular),SOURCE(Serif,Bold),SOURCE(Serif,Italic),SOURCE(Serif,BoldItalic),
                        SOURCE(Sans,Regular),SOURCE(Sans,Bold),SOURCE(Sans,Italic),SOURCE(Sans,BoldItalic)};
}
extern "C" int browser_font_begin(browser_font_atlas *out,int (*budget)()) {
    if(!out || fonts.library || live)return -84;
    atlas=out;atlas->count=atlas->used=0;charge=budget;failed=false;allocations_left=UINT32_MAX;rasterizations=0;return 0;
}
extern "C" void browser_font_finish() { fonts.clear();atlas=nullptr;charge=nullptr; }
extern "C" uint32_t browser_font_live_bytes() { return live; }
extern "C" uint32_t browser_font_rasterizations() { return rasterizations; }
extern "C" void browser_font_fail_after(uint32_t count) { allocations_left=count; }
extern "C" int browser_font_check(const uint8_t *bytes,size_t size) {
    if(!bytes || !size || size>1024U*1024U)return -84;
    if(fonts.open())return -28;
    FT_Face face=nullptr;FT_Error rc=FT_New_Memory_Face(fonts.library,bytes,(FT_Long)size,0,&face);
    if(!rc)rc=FT_Select_Charmap(face,FT_ENCODING_UNICODE);
    if(!rc)rc=FT_Set_Pixel_Sizes(face,0,16);
    if(!rc)rc=FT_Load_Char(face,'W',FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP|FT_LOAD_RENDER);
    if(face)FT_Done_Face(face);
    return failed ? -28 : rc ? -84 : 0;
}
extern "C" int browser_font_get(uint32_t face_id,uint32_t height,uint32_t scalar,const browser_font_glyph **out) {
    if(out)*out=nullptr;
    if(!out || !atlas || !face_id || face_id>8 || !height || height>64 || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))return -84;
    if(failed || (charge && charge()))return -28;
    uint32_t key=browser_font_key(face_id,height,scalar),lo=0,hi=atlas->count;
    while(lo<hi) { uint32_t m=(lo+hi)/2;if(atlas->glyphs[m].key<key)lo=m+1;else hi=m; }
    if(lo<atlas->count && atlas->glyphs[lo].key==key) { *out=&atlas->glyphs[lo];return 0; }
    if(atlas->count==BROWSER_FONT_GLYPHS || fonts.open())return -28;
    FT_Face &face=fonts.faces[face_id-1];const Source &source=sources[face_id-1];
    if(!face && (FT_New_Memory_Face(fonts.library,source.start,(FT_Long)(source.end-source.start),0,&face) ||
                FT_Select_Charmap(face,FT_ENCODING_UNICODE)))return -28;
    FT_UInt glyph=FT_Get_Char_Index(face,scalar);
    /* Missing coverage is an explicit request for the existing Unicode PSF. */
    if(!glyph)return 1;
    if(FT_Set_Pixel_Sizes(face,0,height) || FT_Load_Glyph(face,glyph,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP|FT_LOAD_RENDER))return -28;
    ++rasterizations;
    const FT_Bitmap &bitmap=face->glyph->bitmap;
    int64_t advance=(face->glyph->advance.x+32)/64;
    int64_t baseline=((int64_t)height+(face->size->metrics.ascender+face->size->metrics.descender)/64)/2;
    int64_t left=face->glyph->bitmap_left,top=baseline-face->glyph->bitmap_top;
    if(failed || bitmap.width>96 || bitmap.rows>96 || (bitmap.width && bitmap.rows && (bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY || bitmap.num_grays!=256)) ||
       advance<0 || advance>96 || left< -96 || left>96 || top< -96 || top>96 ||
       bitmap.pitch<0 || (uint32_t)bitmap.pitch<bitmap.width || bitmap.width*bitmap.rows>BROWSER_FONT_ALPHA-atlas->used)return -28;
    uint32_t offset=lo<atlas->count ? atlas->glyphs[lo].offset : atlas->used,bytes=bitmap.width*bitmap.rows;
    browser_font_glyph record={key,offset,(int16_t)advance,(int16_t)left,(int16_t)top,(uint16_t)bitmap.width,(uint16_t)bitmap.rows,0};
    /* Dense, key-ordered payload permits linear UI admission without a
     * quadratic overlap check or an allocation on every paint. */
    memmove(atlas->alpha+offset+bytes,atlas->alpha+offset,atlas->used-offset);
    for(uint32_t y=0;y<bitmap.rows;++y)memcpy(atlas->alpha+offset+y*bitmap.width,bitmap.buffer+y*(uint32_t)bitmap.pitch,bitmap.width);
    for(uint32_t i=lo;i<atlas->count;++i)atlas->glyphs[i].offset+=bytes;
    memmove(atlas->glyphs+lo+1,atlas->glyphs+lo,(atlas->count-lo)*sizeof(record));
    atlas->glyphs[lo]=record;atlas->used+=bitmap.width*bitmap.rows;++atlas->count;*out=&atlas->glyphs[lo];return 0;
}
