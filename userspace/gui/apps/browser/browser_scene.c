#include "browser_scene.h"
#include "html_protocol.h"
#include <string.h>

_Static_assert(sizeof(browser_css_packet_t)==2048U,"bulk packet size");
_Static_assert(sizeof(browser_scene_run_t)==40U,"scene run size");
_Static_assert(sizeof(browser_css_request_t)==444U,"CSS request size");
int browser_css_request_validate(const browser_css_request_t *q) {
    if (!q) return -84;
    const browser_html_header_t *h=&q->header;
    if (h->magic!=BROWSER_HTML_MAGIC || !browser_html_profile_valid(h) || !h->request ||
        !h->parent_pid || !h->parent_generation || h->child_pid || h->child_generation ||
        !h->input_length || h->mode>2 ||
        (h->version==BROWSER_HTML_DOCUMENT_VERSION)!=(q->version==BROWSER_CSS_DOCUMENT_VERSION) ||
        (h->version==BROWSER_HTML_SCRIPT_VERSION)!=(q->version==BROWSER_CSS_SCRIPT_VERSION) ||
        !q->width || q->width>1024 || !q->height || q->height>768) return -84;
    uint32_t original=(uint32_t)sizeof(*q)+h->input_length;
    if(q->version==1U || q->version==BROWSER_SCENE_VERSION) { if(h->size!=original) return -84; }
    else if(q->version==BROWSER_CSS_RESOURCE_VERSION || q->version==BROWSER_CSS_DOCUMENT_VERSION || q->version==BROWSER_CSS_SCRIPT_VERSION) {
        if(h->size<original+BROWSER_RESOURCE_HEADER_BYTES || h->size>original+BROWSER_RESOURCE_WIRE_CAPACITY) return -84;
    } else return -84;
    uint32_t url_length=0;
    while (url_length<sizeof(q->document_url) && q->document_url[url_length]) ++url_length;
    if (!url_length || url_length==sizeof(q->document_url)) return -84;
    for (unsigned i=0;i<16;++i) if (q->image_sizes[i][0]>2048 || q->image_sizes[i][1]>2048 ||
        (!!q->image_sizes[i][0]!=!!q->image_sizes[i][1])) return -84;
    return 0;
}
int browser_css_pack(const browser_html_reply_t *r,const browser_scene_t *s,uint8_t *out,size_t capacity) {
    if (!r || !out || browser_scene_validate(&r->document,s) || capacity<8) return -84;
    const browser_forms_t *f=&s->forms;
    uint32_t runs_size=20+s->count*sizeof(s->runs[0]);
    uint32_t form_size=20+f->form_count*sizeof(f->forms[0])+f->control_count*sizeof(f->controls[0])+
        f->option_count*sizeof(f->options[0])+f->used;
    uint32_t limits_size=f->version==BROWSER_FORMS_VERSION ? f->control_count*sizeof(uint32_t) : 0;
    form_size+=limits_size;
    uint32_t wide_size=s->version>=BROWSER_SCENE_DOCUMENT_VERSION ? r->document.image_count*BROWSER_RESOURCE_URL_CAPACITY : 0;
    uint32_t scene_size=runs_size+form_size+wide_size;
    if (capacity<8+scene_size) return -28;
    int doc_size=browser_html_pack(r,out+8,capacity-8-scene_size);
    if (doc_size<0) return doc_size;
    uint32_t sizes[]={(uint32_t)doc_size,scene_size};
    memcpy(out,sizes,8); memcpy(out+8+doc_size,s,runs_size);
    uint8_t *p=out+8+doc_size+runs_size;
    memcpy(p,f,20); p+=20;
    memcpy(p,f->forms,f->form_count*sizeof(f->forms[0])); p+=f->form_count*sizeof(f->forms[0]);
    memcpy(p,f->controls,f->control_count*sizeof(f->controls[0])); p+=f->control_count*sizeof(f->controls[0]);
    memcpy(p,f->options,f->option_count*sizeof(f->options[0])); p+=f->option_count*sizeof(f->options[0]);
    memcpy(p,f->strings,f->used); p+=f->used;
    if(limits_size) { memcpy(p,f->max_length_plus_one,limits_size); p+=limits_size; }
    if(wide_size) memcpy(p,s->image_urls,wide_size);
    return 8+doc_size+(int)scene_size;
}
int browser_css_unpack(const uint8_t *in,size_t length,const browser_css_request_t *q,
    uint32_t pid,uint32_t generation,browser_html_reply_t *r,browser_scene_t *s) {
    if (!in || !r || !s || browser_css_request_validate(q) || length<28 || length>BROWSER_CSS_WIRE_CAPACITY) return -84;
    uint32_t sizes[2]; memcpy(sizes,in,8);
    if (sizes[0]>sizeof(*r) || sizes[1]<20 || sizes[1]>sizeof(*s) || 8U+sizes[0]+sizes[1]!=length) return -84;
    uint32_t prefix[5]; memcpy(prefix,in+8+sizes[0],20);
    if ((prefix[0]!=BROWSER_SCENE_VERSION && prefix[0]!=BROWSER_SCENE_DOCUMENT_VERSION && prefix[0]!=BROWSER_SCENE_LAYOUT_VERSION) ||
        (prefix[0]>=BROWSER_SCENE_DOCUMENT_VERSION && q->version!=BROWSER_CSS_DOCUMENT_VERSION && q->version!=BROWSER_CSS_SCRIPT_VERSION) || prefix[1]!=q->width || prefix[2]!=q->height ||
        prefix[4]>BROWSER_SCENE_RUNS) return -84;
    if (browser_html_unpack(in+8,sizes[0],r) ||
        browser_html_validate(r,sizeof(*r),&q->header,pid,generation)) return -84;
    uint32_t wide_size=prefix[0]>=BROWSER_SCENE_DOCUMENT_VERSION ? r->document.image_count*BROWSER_RESOURCE_URL_CAPACITY : 0;
    uint32_t runs_size=20U+prefix[4]*sizeof(s->runs[0]), counts[5];
    if(sizes[1]<runs_size+20) return -84;
    const uint8_t *p=in+8+sizes[0]+runs_size; memcpy(counts,p,20);
    if(counts[0]>BROWSER_FORMS_VERSION || counts[1]>BROWSER_FORM_COUNT || counts[2]>BROWSER_FORM_CONTROLS ||
       counts[3]>BROWSER_FORM_OPTIONS || counts[4]>BROWSER_FORM_BYTES ||
       sizes[1]!=runs_size+20+counts[1]*sizeof(browser_form_t)+counts[2]*sizeof(browser_form_control_t)+
           counts[3]*sizeof(browser_form_option_t)+counts[4]+wide_size+
           (counts[0]==BROWSER_FORMS_VERSION ? counts[2]*sizeof(uint32_t) : 0)) return -84;
    memset(s,0,sizeof(*s)); memcpy(s,in+8+sizes[0],runs_size);
    browser_forms_t *f=&s->forms; memcpy(f,counts,20); p+=20;
    memcpy(f->forms,p,counts[1]*sizeof(f->forms[0])); p+=counts[1]*sizeof(f->forms[0]);
    memcpy(f->controls,p,counts[2]*sizeof(f->controls[0])); p+=counts[2]*sizeof(f->controls[0]);
    memcpy(f->options,p,counts[3]*sizeof(f->options[0])); p+=counts[3]*sizeof(f->options[0]);
    memcpy(f->strings,p,counts[4]); p+=counts[4];
    if(counts[0]==BROWSER_FORMS_VERSION) {
        memcpy(f->max_length_plus_one,p,counts[2]*sizeof(uint32_t)); p+=counts[2]*sizeof(uint32_t);
    }
    if(wide_size) memcpy(s->image_urls,p,wide_size);
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
    if (!s || browser_html_document_validate(d) ||
        (s->version!=BROWSER_SCENE_VERSION && s->version!=BROWSER_SCENE_DOCUMENT_VERSION && s->version!=BROWSER_SCENE_LAYOUT_VERSION) ||
        !s->width || s->width>1024 || !s->height || s->height>768 ||
        s->count>BROWSER_SCENE_RUNS || s->total_height>BROWSER_SCENE_COORD_LIMIT || browser_forms_validate(&s->forms)) return -84;
    for(uint32_t i=0;i<REIST_HTML_IMAGE_CAPACITY;++i) {
        const char *url=s->image_urls[i]; uint32_t n=0;
        while(n<BROWSER_RESOURCE_URL_CAPACITY && url[n]) ++n;
        if(n==BROWSER_RESOURCE_URL_CAPACITY || (n &&
            (s->version<BROWSER_SCENE_DOCUMENT_VERSION || i>=d->image_count || d->images[i].source[0] || n<256))) return -84;
    }
    uint32_t text_bytes=0;
    for (uint32_t i=0;i<s->count;++i) {
        const browser_scene_run_t *r=&s->runs[i];
        if (r->x<-BROWSER_SCENE_COORD_LIMIT || r->x>BROWSER_SCENE_COORD_LIMIT ||
            r->y<-BROWSER_SCENE_COORD_LIMIT || r->y>BROWSER_SCENE_COORD_LIMIT ||
            r->width>BROWSER_SCENE_COORD_LIMIT || r->height>BROWSER_SCENE_COORD_LIMIT ||
            (int64_t)r->y+r->height>BROWSER_SCENE_COORD_LIMIT || (int64_t)r->x+r->width>BROWSER_SCENE_COORD_LIMIT ||
            (r->flags&~(s->version==BROWSER_SCENE_LAYOUT_VERSION ? 195U : 67U)) ||
            (r->link!=UINT32_MAX && r->link>=d->link_count) ||
            (!!(r->flags&64U)!=(r->link!=UINT32_MAX))) return -84;
        if (r->kind==REIST_HTML_ELEMENT_TEXT) {
            if (!r->height || r->height>BROWSER_CSS_FONT_MAX || !r->length || r->length>128 || r->offset>d->text_length ||
                r->length>d->text_length-r->offset || !boundary(d,r->offset) || !boundary(d,r->offset+r->length)) return -84;
            text_bytes+=r->length; if (text_bytes>REIST_HTML_TEXT_CAPACITY) return -84;
            uint32_t scalars=0;
            for (uint32_t j=0;j<r->length;++j) if (((uint8_t)d->text[r->offset+j]&0xc0)!=0x80) ++scalars;
            if (r->width!=scalars*((r->height+1)/2)) return -84;
        } else if (r->kind==REIST_HTML_ELEMENT_IMAGE) {
            if (r->offset>=d->image_count || r->length || !r->width || !r->height) return -84;
        } else if (r->kind==REIST_HTML_ELEMENT_ANCHOR) {
            if (r->offset>=d->anchor_count || r->length || r->width || r->height || r->flags || r->link!=UINT32_MAX) return -84;
        } else if (r->kind==BROWSER_SCENE_CONTROL) {
            if(r->offset>=s->forms.control_count || !r->width || !r->height || r->width>1024 || r->height>768 ||
               r->length || r->flags || r->link!=UINT32_MAX || s->forms.controls[r->offset].kind==BROWSER_FORM_HIDDEN) return -84;
            for(uint32_t j=0;j<i;++j) if(s->runs[j].kind==BROWSER_SCENE_CONTROL && s->runs[j].offset==r->offset) return -84;
        } else if (r->kind==BROWSER_SCENE_FILL) {
            if (r->offset || r->length || r->flags || r->link!=UINT32_MAX) return -84;
        } else if(r->kind==BROWSER_SCENE_ROUND || r->kind==BROWSER_SCENE_SHADOW) {
            if(s->version!=BROWSER_SCENE_LAYOUT_VERSION || r->offset>64 || r->length>32 ||
               (r->flags&~64U) || (r->kind==BROWSER_SCENE_SHADOW && (r->flags || r->link!=UINT32_MAX))) return -84;
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
static int round_inside(int32_t x,int32_t y,int32_t width,int32_t height,int32_t radius) {
    if(x<0 || y<0 || x>=width || y>=height) return 0;
    if(radius>width/2) radius=width/2;
    if(radius>height/2) radius=height/2;
    int32_t dx=x<radius ? 2*(radius-x)-1 : x>=width-radius ? 2*(x-(width-radius))+1 : 0;
    int32_t dy=y<radius ? 2*(radius-y)-1 : y>=height-radius ? 2*(y-(height-radius))+1 : 0;
    return !dx || !dy || dx*dx+dy*dy<=4*radius*radius;
}
/* One bounded rounded fill/stroke or soft shadow run. No scratch allocation,
 * style lookup or I/O in paint. Radius <=64, blur <=32; eight coverage samples. */
static uint32_t round_coverage(const browser_scene_run_t *r,int32_t x,int32_t y) {
    int32_t width=(int32_t)r->width,height=(int32_t)r->height,radius=(int32_t)r->offset;
    if(r->kind==BROWSER_SCENE_ROUND) {
        int32_t stroke=(int32_t)r->length;
        if(!round_inside(x,y,width,height,radius)) return 0;
        if(stroke && round_inside(x-stroke,y-stroke,width-2*stroke,height-2*stroke,radius>stroke ? radius-stroke : 0)) return 0;
        return 255;
    }
    int32_t blur=(int32_t)r->length;
    if(!blur) return round_inside(x,y,width,height,radius) ? 255 : 0;
    if(!round_inside(x,y,width,height,radius+blur)) return 0;
    if(round_inside(x-2*blur,y-2*blur,width-4*blur,height-4*blur,radius>blur ? radius-blur : 0)) return 255;
    uint32_t coverage=1;
    for(int32_t i=1;i<7;++i) {
        int32_t inset=2*blur*i/7;
        coverage+=round_inside(x-inset,y-inset,width-2*inset,height-2*inset,radius+blur-inset>0 ? radius+blur-inset : 0) ? 1 : 0;
    }
    return coverage*255/8;
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
 * 128 direct-mapped slots bound memory and lookup work through 64px glyphs. */
#define BROWSER_GLYPH_SLOTS 128U
typedef struct browser_glyph_scratch {
    uint32_t glyph, height, pixels[BROWSER_CSS_FONT_MAX*(BROWSER_CSS_FONT_MAX/2)];
} browser_glyph_scratch_t;
static int document_glyph(const reist_gui_font_t *font,uint32_t glyph,uint32_t cell,
    uint32_t height,uint32_t *pixels) {
    if(height<=REIST_GUI_FONT_MAX_HEIGHT)
        return reist_gui_font_raster_scaled_xrgb(font,glyph,cell,height,0xffffff,0,
            pixels,cell,BROWSER_CSS_FONT_MAX*(BROWSER_CSS_FONT_MAX/2));
    /* The shared font decoder retains its 32px contract. Scale its validated
     * native bitmap here; larger document typography grants no decoder input
     * authority and remains bounded to 32x64 destination pixels. */
    uint32_t native[REIST_GUI_FONT_MAX_WIDTH*REIST_GUI_FONT_MAX_HEIGHT];
    if(height>BROWSER_CSS_FONT_MAX || cell>BROWSER_CSS_FONT_MAX/2 ||
        reist_gui_font_raster_scaled_xrgb(font,glyph,font->width,font->height,
            0xffffff,0,native,font->width,sizeof(native)/sizeof(native[0]))) return -84;
    for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<cell;++x)
        pixels[y*cell+x]=native[(y*font->height/height)*font->width+x*font->width/cell];
    return 0;
}
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
            if (document_glyph(font,glyph,cell,h,cached->pixels)) return -84;
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
int browser_scene_raster_forms(const reist_html_document_t *d,const browser_scene_t *s,
    const reist_gui_font_t *font,const browser_image_slot_t *images,const browser_form_state_t *forms,uint32_t scroll,
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
        if(r->kind==BROWSER_SCENE_CONTROL) {
            const browser_form_control_t *c=&s->forms.controls[r->offset];
            if(c->kind==BROWSER_FORM_LABEL) continue;
            uint32_t color=c->flags&BROWSER_FORM_DISABLED ? 0xff808080 : 0xff202020;
            int button=c->kind==BROWSER_FORM_SUBMIT || c->kind==BROWSER_FORM_RESET || c->kind==BROWSER_FORM_BUTTON;
            if(c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO) {
                int32_t side=(int32_t)(r->width<r->height ? r->width : r->height); if(side>18) side=18;
                int32_t ox=r->x+((int32_t)r->width-side)/2,oy=y+((int32_t)r->height-side)/2;
                int checked=forms ? forms->checked[r->offset] : !!(c->flags&BROWSER_FORM_CHECKED);
                for(int32_t yy=t;yy<end_y;++yy) for(int32_t xx=l;xx<end_x;++xx) {
                    int32_t dx=xx-ox,dy=yy-oy;
                    if(dx<0 || dy<0 || dx>=side || dy>=side) continue;
                    uint32_t shade=0xffffff;
                    if(c->kind==BROWSER_FORM_RADIO) {
                        int32_t cx=2*dx-side+1,cy=2*dy-side+1,d=cx*cx+cy*cy;
                        if(d>(side-1)*(side-1)) continue;
                        if(d>(side-3)*(side-3)) shade=0x808080;
                        else if(checked && d<=(side/3)*(side/3)) shade=color&0xffffff;
                    } else {
                        if(!dx || !dy) shade=0x808080;
                        else if(dx==side-1 || dy==side-1) shade=0xd4d0c8;
                        int32_t mid=side/2-1,target=dx<=mid ? side/2+dx-4 : side/2+2*mid-dx-4;
                        if(checked && side>=12 && dx>=4 && dx<side-4 && dy>=target-1 && dy<=target+1) shade=color&0xffffff;
                    }
                    pixels[(uint32_t)yy*width+(uint32_t)xx]=shade;
                }
                continue;
            }
            for(int32_t yy=t;yy<end_y;++yy) for(int32_t xx=l;xx<end_x;++xx) {
                uint32_t shade=button ? 0xd4d0c8 : 0xffffff;
                if(xx==r->x || yy==y) shade=button ? 0xffffff : 0x808080;
                if(xx==r->x+(int32_t)r->width-1 || yy==y+(int32_t)r->height-1) shade=button ? 0x808080 : 0x404040;
                if(c->kind==BROWSER_FORM_SELECT && r->width>=24 && xx>=r->x+(int32_t)r->width-18) {
                    shade=0xd4d0c8;
                    if(xx==r->x+(int32_t)r->width-18) shade=0x808080;
                    int32_t dx=xx-(r->x+(int32_t)r->width-9),dy=yy-(y+(int32_t)r->height/2-2);
                    if(dy>=0 && dy<5 && dx>=dy-4 && dx<=4-dy) shade=color&0xffffff;
                }
                pixels[(uint32_t)yy*width+(uint32_t)xx]=shade;
            }
            const char *v=forms ? browser_forms_value(&s->forms,forms,r->offset) : s->forms.strings+c->value;
            if(button) v=s->forms.strings+c->label;
            if(c->kind==BROWSER_FORM_UNSUPPORTED) v="[unsupported]";
            if(c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO)
                v=(forms ? forms->checked[r->offset] : !!(c->flags&BROWSER_FORM_CHECKED)) ? "x" : "";
            if(c->kind==BROWSER_FORM_SELECT) {
                v="";
                for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j)
                    if(forms ? forms->selected[j] : !!(s->forms.options[j].flags&BROWSER_FORM_CHECKED)) { v=s->forms.strings+s->forms.options[j].label; break; }
            }
            uint32_t inset=c->kind==BROWSER_FORM_SELECT ? 24 : 8;
            uint32_t at=0, row=0, cells=r->width>inset ? (r->width-inset)/8 : 0;
            int32_t text_x=r->x+4,text_y=y+4;
            if(button) {
                uint32_t count=0;
                for(uint32_t j=0;v[j] && count<=cells;++j) if(((uint8_t)v[j]&192)!=128) ++count;
                if(count<=cells) text_x=r->x+((int32_t)r->width-(int32_t)count*8)/2;
                if(r->height>=16) text_y=y+((int32_t)r->height-16)/2;
            }
            while(v[at] && row*16+20<=r->height) {
                uint32_t start=at,scalars=0;
                while(v[at] && v[at]!='\n' && v[at]!='\r' && scalars<cells && at-start<124) {
                    ++at; while(((uint8_t)v[at]&192)==128) ++at; ++scalars;
                }
                if(at>start && font && text_pixels(font,v+start,at-start,text_x,text_y+(int32_t)row*16,16,color,0,pixels,width,end_x,t,end_y,scratch)) return -84;
                if(!cells || at==start) { if(v[at]=='\r' && v[at+1]=='\n') ++at; if(v[at]) ++at; }
                else if(v[at]=='\n' || v[at]=='\r') { if(v[at]=='\r' && v[at+1]=='\n') ++at; ++at; }
                if(c->kind!=BROWSER_FORM_TEXTAREA) break;
                ++row;
            }
        } else if (r->kind==BROWSER_SCENE_FILL || r->kind==BROWSER_SCENE_ROUND || r->kind==BROWSER_SCENE_SHADOW) {
            if (!(r->color>>24)) continue;
            for (int32_t yy=t;yy<end_y;++yy) for (int32_t xx=l;xx<end_x;++xx) {
                uint32_t *p=&pixels[(uint32_t)yy*width+(uint32_t)xx];
                *p=blend(*p,r->color,r->kind==BROWSER_SCENE_FILL ? 255 : round_coverage(r,xx-r->x,yy-y));
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
        if ((r->flags&64U) && !(r->flags&BROWSER_SCENE_NO_UNDERLINE) && (r->kind==1 || r->kind==5) && r->height>=2 && y+(int32_t)r->height-2>=(int32_t)top && y+(int32_t)r->height-2<bottom)
            for (int32_t xx=l;xx<end_x;++xx) pixels[(uint32_t)(y+(int32_t)r->height-2)*width+(uint32_t)xx]=r->kind==1 ? r->color&0xffffff : 0xcc;
    }
    return 0;
}
int browser_scene_raster(const reist_html_document_t *d,const browser_scene_t *s,
    const reist_gui_font_t *font,const browser_image_slot_t *images,uint32_t scroll,
    uint32_t *pixels,uint32_t width,uint32_t height,uint32_t top,uint32_t view) {
    return browser_scene_raster_forms(d,s,font,images,NULL,scroll,pixels,width,height,top,view);
}
