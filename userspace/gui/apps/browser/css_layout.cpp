#include "css_layout.hpp"
extern "C" int browser_css_box_size(int32_t value,int32_t minimum,int32_t maximum,int32_t extra,int border_box,int32_t *out) {
    if(value<0 || minimum<0 || maximum<0 || extra<0 || value>262144 || minimum>262144 || maximum>262144 || extra>262144 || !out) return -28;
    if(value>maximum) value=maximum;
    if(value<minimum) value=minimum;
    if(border_box) value=value>extra ? value-extra : 0;
    if(value>262144-extra) return -28;
    *out=value; return 0;
}
namespace {
int32_t clamp(int32_t value,int32_t lo,int32_t hi) { return value<lo ? lo : value>hi ? hi : value; }
}
extern "C" int browser_css_flex_line(browser_css_flex_item *items,uint32_t count,int32_t available,int32_t gap,uint32_t justify,uint32_t reverse) {
    if(!items || count>BROWSER_CSS_ITEMS || available<0 || available>262144 || gap<0 || gap>262144 || justify>5 || reverse>1) return -28;
    if(!count) return 0;
    int64_t base=(int64_t)(count-1)*gap;
    bool frozen[BROWSER_CSS_ITEMS]={};
    for(uint32_t i=0;i<count;++i) {
        auto &v=items[i];
        if(v.basis<0 || v.basis>262144 || v.minimum<0 || v.minimum>262144 || v.maximum<v.minimum || v.maximum>262144 ||
           v.grow<0 || v.grow>65536 || v.shrink<0 || v.shrink>65536 || v.before< -262144 || v.before>262144 || v.after< -262144 || v.after>262144) return -28;
        v.size=clamp(v.basis,v.minimum,v.maximum); base+=(int64_t)v.size+v.before+v.after;
    }
    bool grow=base<available;
    int64_t original_free=available-(int64_t)(count-1)*gap;
    for(uint32_t i=0;i<count;++i) {
        auto &v=items[i]; original_free-=(int64_t)v.basis+v.before+v.after;
        frozen[i]=(grow ? !v.grow || v.basis>v.size : !v.shrink || v.basis<v.size);
        if(!frozen[i]) v.size=v.basis;
    }
    for(uint32_t turn=0;turn<=count;++turn) {
        int64_t free=available-(int64_t)(count-1)*gap,weight=0,factors=0;
        for(uint32_t i=0;i<count;++i) {
            auto &v=items[i]; free-=(int64_t)(frozen[i] ? v.size : v.basis)+v.before+v.after;
            if(!frozen[i]) { factors+=grow ? v.grow : v.shrink; weight+=grow ? v.grow : (int64_t)v.shrink*v.basis; }
        }
        if(!weight) break;
        if(factors<1024) {
            int64_t limited=original_free*factors/1024;
            if((limited<0 ? -limited : limited)<(free<0 ? -free : free)) free=limited;
        }
        int64_t carry=0,violation=0;
        int32_t violations[BROWSER_CSS_ITEMS]={};
        for(uint32_t i=0;i<count;++i) if(!frozen[i]) {
            auto &v=items[i]; int64_t w=grow ? v.grow : (int64_t)v.shrink*v.basis;
            int64_t scaled=free*w+carry,delta=scaled/weight; carry=scaled%weight;
            int64_t proposed=(int64_t)v.basis+delta;
            int32_t used=proposed<v.minimum ? v.minimum : proposed>v.maximum ? v.maximum : (int32_t)proposed;
            if(proposed<INT32_MIN || proposed>INT32_MAX) return -28;
            violations[i]=used-(int32_t)proposed; violation+=violations[i]; v.size=used;
        }
        if(!violation) break;
        bool progress=false;
        for(uint32_t i=0;i<count;++i) if(!frozen[i] && (violation>0 ? violations[i]>0 : violations[i]<0)) { frozen[i]=true; progress=true; }
        if(!progress || turn==count) return -28;
    }
    int64_t used=(int64_t)(count-1)*gap;
    for(uint32_t i=0;i<count;++i) used+=(int64_t)items[i].size+items[i].before+items[i].after;
    int64_t extra=available-used,offset=0;
    if(justify==1) offset=extra;
    if(justify==2) offset=extra/2;
    /* space-between/around/evenly fall back when free space is negative. */
    if(extra>0 && justify==4) offset=extra/(2*count);
    if(extra>0 && justify==5) offset=extra/(count+1);
    for(uint32_t i=0;i<count;++i) {
        auto &v=items[i]; int64_t spacing=0;
        if(extra>0 && justify==3 && count>1) spacing=extra*i/(count-1);
        if(extra>0 && justify==4) spacing=extra*i/count;
        if(extra>0 && justify==5) spacing=extra*i/(count+1);
        int64_t position=offset+spacing+v.before;
        if(reverse) position=available-position-v.size;
        if(position< -262144 || position>262144) return -28;
        v.position=(int32_t)position; offset+=(int64_t)v.before+v.size+v.after+gap;
    }
    return 0;
}
extern "C" int browser_css_grid_columns(int32_t *out,const int32_t *minimum,const int32_t *fractions,uint32_t count,int32_t width,int32_t gap) {
    if(!out || !minimum || !fractions || !count || count>16 || width<0 || width>262144 || gap<0 || gap>262144) return -28;
    int64_t available=width-(int64_t)gap*(count-1); bool frozen[16]={};
    for(uint32_t i=0;i<count;++i) {
        if(minimum[i]<0 || minimum[i]>262144 || fractions[i]<0 || fractions[i]>65536) return -28;
        out[i]=minimum[i]; if(!fractions[i]) { frozen[i]=true; available-=minimum[i]; }
    }
    for(uint32_t turn=0;turn<=count;++turn) {
        int64_t weight=0; for(uint32_t i=0;i<count;++i) if(!frozen[i]) weight+=fractions[i];
        if(!weight) break;
        if(weight<1024) weight=1024; /* fractional tracks below 1fr leave space */
        bool progress=false,freeze[16]={};
        for(uint32_t i=0;i<count;++i) if(!frozen[i] && available*fractions[i]/weight<minimum[i]) {
            freeze[i]=true; progress=true;
        }
        if(progress) { for(uint32_t i=0;i<count;++i) if(freeze[i]) { frozen[i]=true; available-=minimum[i]; } continue; }
        int64_t carry=0;
        for(uint32_t i=0;i<count;++i) if(!frozen[i]) {
            int64_t n=available*fractions[i]+carry; int64_t value=n/weight; carry=n%weight;
            if(value<0 || value>262144) return -28; out[i]=(int32_t)value;
        }
        break;
    }
    return 0;
}
