#include "browser_scene.h"
#include "html_protocol.h"
#include <string.h>

_Static_assert(sizeof(browser_css_packet_t)==2048U,"bulk packet size");
_Static_assert(sizeof(browser_scene_run_t)==40U,"scene run size");
_Static_assert(sizeof(browser_css_request_t)==444U,"CSS request size");
int browser_css_request_validate(const browser_css_request_t *q) {
    if (!q) return -84;
    const browser_html_header_t *h=&q->header;
    if (h->magic!=BROWSER_HTML_MAGIC || h->version!=BROWSER_HTML_VERSION || !h->request ||
        !h->parent_pid || !h->parent_generation || h->child_pid || h->child_generation ||
        !h->input_length || h->input_length>REIST_HTML_INPUT_CAPACITY ||
        h->size!=sizeof(*q)+h->input_length || h->mode>2 || h->reserved[0] || h->reserved[1] ||
        q->version!=BROWSER_SCENE_VERSION || !q->width || q->width>1024 || !q->height || q->height>768) return -84;
    uint32_t url_length=0;
    while (url_length<sizeof(q->document_url) && q->document_url[url_length]) ++url_length;
    if (!url_length || url_length==sizeof(q->document_url)) return -84;
    for (unsigned i=0;i<16;++i) if (q->image_sizes[i][0]>2048 || q->image_sizes[i][1]>2048 ||
        (!!q->image_sizes[i][0]!=!!q->image_sizes[i][1])) return -84;
    return 0;
}
int browser_css_pack(const browser_html_reply_t *r,const browser_scene_t *s,uint8_t *out,size_t capacity) {
    if (!r || !out || browser_scene_validate(&r->document,s) || capacity<8) return -84;
    uint32_t scene_size=20+s->count*sizeof(s->runs[0]);
    if (capacity<8+scene_size) return -28;
    int doc_size=browser_html_pack(r,out+8,capacity-8-scene_size);
    if (doc_size<0) return doc_size;
    uint32_t sizes[]={(uint32_t)doc_size,scene_size};
    memcpy(out,sizes,8); memcpy(out+8+doc_size,s,scene_size);
    return 8+doc_size+(int)scene_size;
}
int browser_css_unpack(const uint8_t *in,size_t length,const browser_css_request_t *q,
    uint32_t pid,uint32_t generation,browser_html_reply_t *r,browser_scene_t *s) {
    if (!in || !r || !s || browser_css_request_validate(q) || length<28 || length>BROWSER_CSS_WIRE_CAPACITY) return -84;
    uint32_t sizes[2]; memcpy(sizes,in,8);
    if (sizes[0]>sizeof(*r) || sizes[1]<20 || sizes[1]>sizeof(*s) || 8U+sizes[0]+sizes[1]!=length) return -84;
    uint32_t prefix[5]; memcpy(prefix,in+8+sizes[0],20);
    if (prefix[0]!=BROWSER_SCENE_VERSION || prefix[1]!=q->width || prefix[2]!=q->height ||
        prefix[4]>BROWSER_SCENE_RUNS || sizes[1]!=20U+prefix[4]*sizeof(s->runs[0])) return -84;
    if (browser_html_unpack(in+8,sizes[0],r) ||
        browser_html_validate(r,sizeof(*r),&q->header,pid,generation)) return -84;
    memset(s,0,sizeof(*s)); memcpy(s,in+8+sizes[0],sizes[1]);
    return browser_scene_validate(&r->document,s);
}
int browser_css_packet_accept(const browser_css_packet_t *p,uint32_t length,uint32_t request,
    uint8_t *out,uint32_t capacity,uint32_t *offset,uint32_t *total) {
    if (!p || !out || !offset || !total || length<=16 || length>sizeof(*p) ||
        p->magic!=BROWSER_CSS_PACKET_MAGIC || p->request!=request || !request || !p->total ||
        p->total>capacity || p->offset!=*offset || *offset>p->total ||
        length-16>p->total-*offset || (*total && *total!=p->total)) return -84;
    memcpy(out+*offset,p->bytes,length-16); *offset+=length-16; *total=p->total; return 0;
}

static int boundary(const reist_html_document_t *d,uint32_t at) {
    return at==d->text_length || ((uint8_t)d->text[at]&0xc0)!=0x80;
}
int browser_scene_validate(const reist_html_document_t *d,const browser_scene_t *s) {
    if (!s || browser_html_document_validate(d) || s->version!=BROWSER_SCENE_VERSION ||
        !s->width || s->width>1024 || !s->height || s->height>768 ||
        s->count>BROWSER_SCENE_RUNS || s->total_height>BROWSER_SCENE_COORD_LIMIT) return -84;
    uint32_t text_bytes=0;
    for (uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i];
        if (r->x<-BROWSER_SCENE_COORD_LIMIT || r->x>BROWSER_SCENE_COORD_LIMIT ||
            r->y<-BROWSER_SCENE_COORD_LIMIT || r->y>BROWSER_SCENE_COORD_LIMIT ||
            r->width>BROWSER_SCENE_COORD_LIMIT || r->height>BROWSER_SCENE_COORD_LIMIT ||
            (int64_t)r->y+r->height>BROWSER_SCENE_COORD_LIMIT || (r->flags&~67U) ||
            (r->link!=UINT32_MAX && r->link>=d->link_count) ||
            (!!(r->flags&64U)!=(r->link!=UINT32_MAX))) return -84;
        if (r->kind==REIST_HTML_ELEMENT_TEXT) {
            if (!r->height || r->height>32 || !r->length || r->length>128 || r->offset>d->text_length ||
                r->length>d->text_length-r->offset || !boundary(d,r->offset) || !boundary(d,r->offset+r->length)) return -84;
            text_bytes+=r->length; if (text_bytes>REIST_HTML_TEXT_CAPACITY) return -84;
            uint32_t scalars=0;
            for (uint32_t j=0;j<r->length;++j) if (((uint8_t)d->text[r->offset+j]&0xc0)!=0x80) ++scalars;
            if (r->width!=scalars*((r->height+1)/2)) return -84;
        } else if (r->kind==REIST_HTML_ELEMENT_IMAGE) {
            if (r->offset>=d->image_count || r->length || !r->width || !r->height) return -84;
        } else if (r->kind==REIST_HTML_ELEMENT_ANCHOR) {
            if (r->offset>=d->anchor_count || r->length || r->width || r->height || r->flags || r->link!=UINT32_MAX) return -84;
        } else if (r->kind==BROWSER_SCENE_FILL) {
            if (r->offset || r->length || r->flags || r->link!=UINT32_MAX) return -84;
        } else return -84;
    }
    return 0;
}
static uint32_t blend(uint32_t background,uint32_t foreground,uint32_t coverage) {
    uint32_t alpha=(foreground>>24)*coverage/255;
    if (alpha==255) return foreground&0xffffff;
    if (!alpha) return background;
    uint32_t red=(((foreground>>16)&255)*alpha+((background>>16)&255)*(255-alpha))/255;
    uint32_t green=(((foreground>>8)&255)*alpha+((background>>8)&255)*(255-alpha))/255;
    uint32_t blue=((foreground&255)*alpha+(background&255)*(255-alpha))/255;
    return (red<<16)|(green<<8)|blue;
}
static uint32_t decode(const char *text,uint32_t length,uint32_t *at) {
    uint32_t c=(uint8_t)text[(*at)++];
    unsigned more=c<0x80 ? 0 : c<0xe0 ? 1 : c<0xf0 ? 2 : 3;
    if (more) c&=(1U<<(6-more))-1;
    while (more-- && *at<length) c=(c<<6)|((uint8_t)text[(*at)++]&63U);
    return c;
}
/* Private, single-threaded renderer scratch, not a font/resource cache across
 * frames. Each invocation invalidates the tags before using its current font.
 * 128 direct-mapped slots bound both memory (263168 bytes) and lookup work. */
#define BROWSER_GLYPH_SLOTS 128U
typedef struct browser_glyph_scratch {
    uint32_t glyph, height, pixels[32*16];
} browser_glyph_scratch_t;
static int text_pixels(const reist_gui_font_t *font,const char *text,uint32_t length,
    int32_t x,int32_t y,uint32_t h,uint32_t color,uint32_t flags,uint32_t *pixels,
    uint32_t stride,int32_t right,int32_t top,int32_t bottom,
    browser_glyph_scratch_t *scratch) {
    if (!font) return -22;
    uint32_t cell=(h+1)/2;
    for (uint32_t at=0;at<length;x+=(int32_t)cell) {
        uint32_t scalar=decode(text,length,&at), glyph;
        if (x>=right || x+(int32_t)cell+8<=0) continue;
        if (reist_gui_font_lookup(font,scalar,&glyph)<0) return -84;
        browser_glyph_scratch_t *cached=&scratch[(glyph^h)%BROWSER_GLYPH_SLOTS];
        if (cached->height!=h || cached->glyph!=glyph) {
            cached->height=0;
            if (reist_gui_font_raster_scaled_xrgb(font,glyph,cell,h,0xffffff,0,cached->pixels,cell,32*16)) return -84;
            cached->glyph=glyph; cached->height=h;
        }
        const uint32_t *glyph_pixels=cached->pixels;
        for (uint32_t yy=0;yy<h;++yy) {
            int32_t py=y+(int32_t)yy; if (py<top || py>=bottom) continue;
            int32_t shear=(flags&2) ? (int32_t)(h-1-yy)/4 : 0;
            for (uint32_t xx=0;xx<cell;++xx) {
                uint32_t coverage=glyph_pixels[yy*cell+xx]&255; if (!coverage) continue;
                for (unsigned bold=0;bold<=((flags&1) ? 1U : 0U);++bold) {
                    int32_t px=x+(int32_t)xx+shear+(int32_t)bold;
                    if (px<0 || px>=right) continue;
                    uint32_t *p=&pixels[(uint32_t)py*stride+(uint32_t)px]; *p=blend(*p,color,coverage);
                }
            }
        }
    }
    return 0;
}
int browser_scene_raster(const reist_html_document_t *d,const browser_scene_t *s,
    const reist_gui_font_t *font,const browser_image_slot_t *images,uint32_t scroll,
    uint32_t *pixels,uint32_t width,uint32_t height,uint32_t top,uint32_t view) {
    if (!pixels || browser_scene_validate(d,s) || !width || width>1024 || height>768 ||
        top>height || view>height-top || scroll>BROWSER_SCENE_COORD_LIMIT) return -84;
    static browser_glyph_scratch_t scratch[BROWSER_GLYPH_SLOTS];
    for (unsigned i=0;i<BROWSER_GLYPH_SLOTS;++i) scratch[i].height=0;
    const int32_t right=(int32_t)(s->width<width ? s->width : width),bottom=(int32_t)(top+view);
    uint64_t work=0;
    /* Admission before mutation: overlapping visible boxes cannot make raster
     * work unbounded. Four million pixel operations is a per-frame quota. */
    for (uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i];
        int32_t y=(int32_t)top+r->y-(int32_t)scroll;
        int32_t l=r->x>0 ? r->x : 0,t=y>(int32_t)top ? y : (int32_t)top;
        int32_t end_x=r->x+(int32_t)r->width+8,end_y=y+(int32_t)r->height;
        if (end_x>right) end_x=right; if (end_y>bottom) end_y=bottom;
        if (end_x>l && end_y>t) work+=(uint64_t)(end_x-l)*(uint32_t)(end_y-t)*(r->kind==1 ? 2U : 1U);
        if (work>4U*1024U*1024U) return -28;
    }
    for (uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i]; int32_t y=(int32_t)top+r->y-(int32_t)scroll;
        if (y+(int32_t)r->height<=(int32_t)top || y>=bottom) continue;
        int32_t l=r->x>0 ? r->x : 0,t=y>(int32_t)top ? y : (int32_t)top;
        int32_t end_x=r->x+(int32_t)r->width,end_y=y+(int32_t)r->height;
        if (end_x>right) end_x=right; if (end_y>bottom) end_y=bottom;
        if (r->kind==BROWSER_SCENE_FILL) {
            if (!(r->color>>24)) continue;
            for (int32_t yy=t;yy<end_y;++yy) for (int32_t xx=l;xx<end_x;++xx) {
                uint32_t *p=&pixels[(uint32_t)yy*width+(uint32_t)xx]; *p=blend(*p,r->color,255);
            }
        } else if (r->kind==1) {
            if (text_pixels(font,d->text+r->offset,r->length,r->x,y,r->height,r->color,r->flags,pixels,width,right,(int32_t)top,bottom,scratch)) return -84;
        } else if (r->kind==5) {
            const browser_image_slot_t *image=images && r->offset<BROWSER_IMAGE_CACHE_COUNT ? &images[r->offset] : NULL;
            if (image && image->decoded) {
                if (!image->width || !image->height || image->width>256 || image->height>256) return -84;
                for (int32_t yy=t;yy<end_y;++yy) for (int32_t xx=l;xx<end_x;++xx) {
                    uint32_t sx=(uint32_t)(xx-r->x)*image->width/r->width,sy=(uint32_t)(yy-y)*image->height/r->height;
                    pixels[(uint32_t)yy*width+(uint32_t)xx]=image->pixels[sy*image->width+sx];
                }
            } else {
                const char *alt=d->images[r->offset].alt; uint32_t n=0; while (alt[n]) ++n;
                if (n && font && text_pixels(font,alt,n,r->x,y,16,0xff606060,0,pixels,width,right,(int32_t)top,bottom,scratch)) return -84;
            }
        }
        if ((r->flags&64U) && r->height>=2 && y+(int32_t)r->height-2>=(int32_t)top && y+(int32_t)r->height-2<bottom)
            for (int32_t xx=l;xx<end_x;++xx) pixels[(uint32_t)(y+(int32_t)r->height-2)*width+(uint32_t)xx]=r->kind==1 ? r->color&0xffffff : 0xcc;
    }
    return 0;
}
