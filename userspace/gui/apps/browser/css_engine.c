/* CSS policy and normal-flow layout live exclusively in the disposable worker.
 * LibCSS owns tokenization, selectors and cascade. No resource callbacks fetch. */
#include "css_engine.h"
#include "css_values.hpp"
#include "css_layout.hpp"
#include "html_engine.h"
#include <libcss/libcss.h>
#include <hubbub/types.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CSS_WORK_LIMIT 262144U
#define CSS_SHEETS 64U
static uint32_t work, failed;
static uint32_t document_profile;
static css_select_handler handler;
static css_select_ctx *context;
static css_stylesheet *sheets[CSS_SHEETS];
static uint32_t sheet_count;
static css_unit_ctx units;
static css_media media;
static const uint32_t (*intrinsic)[2];
static reist_html_document_t *doc;
static browser_scene_t *scene;
static char sheet_bytes[65536];
static const char *document_url;
static const browser_resources_t *resources;
static browser_resource_needs_t *resource_needs;
static css_stylesheet *imported[128];
static uint32_t imported_count;
static const char *import_path[BROWSER_RESOURCE_DEPTH+1];
static char import_reference[BROWSER_RESOURCE_DEPTH+1][BROWSER_RESOURCE_URL_CAPACITY];
static char import_canonical[BROWSER_RESOURCE_DEPTH+1][BROWSER_RESOURCE_URL_CAPACITY];

static css_error budget(void) {
    if (failed || ++work>CSS_WORK_LIMIT) { failed=1; return CSS_NOMEM; }
    return CSS_OK;
}
static int css_value_budget(void) { return (int)budget(); }
static int tag(const node *n, const char *name) {
    return n && n->kind==1 && n->ns==HUBBUB_NS_HTML && !strcmp(n->name,name);
}
static const char *attr(node *n, const char *name) {
    for (attribute *a=n->attributes;a;a=a->next) {
        if (budget()) return NULL;
        if (!strcmp(a->name,name)) return a->value;
    }
    return NULL;
}
static int space(unsigned c) { return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f'; }
static node *parent(node *n) { return n->parent && n->parent->kind==1 ? n->parent : NULL; }
static node *sibling(node *n) {
    for (n=n->previous;n && n->kind!=1;n=n->previous) if (budget()) return NULL;
    return n;
}
static int equal(const char *a, lwc_string *b, int fold) {
    size_t size=lwc_string_length(b);
    return a && strlen(a)==size && !(fold ? strncasecmp(a,lwc_string_data(b),size) : memcmp(a,lwc_string_data(b),size));
}
static int qmatch(node *n, const css_qname *q) {
    return n && n->kind==1 && (!q->ns || !lwc_string_length(q->ns) || equal("*",q->ns,0) ||
        (n->ns==HUBBUB_NS_HTML && equal("http://www.w3.org/1999/xhtml",q->ns,0))) &&
        (equal("*",q->name,0) || equal(n->name,q->name,1));
}
static css_error intern(const char *s, size_t n, lwc_string **out) {
    return lwc_intern_string(s,n,out)==lwc_error_ok ? CSS_OK : CSS_NOMEM;
}
static css_error node_name(void *pw, void *v, css_qname *q) {
    (void)pw; node *n=v; q->ns=NULL;
    if (budget()) return CSS_NOMEM;
    return intern(n->name,strlen(n->name),&q->name);
}
static css_error node_classes(void *pw, void *v, lwc_string ***result, uint32_t *count) {
    (void)pw; *result=NULL; *count=0;
    const char *text=attr(v,"class"); if (!text) return budget();
    lwc_string *local[32]; uint32_t used=0;
    while (*text) {
        while (space((unsigned char)*text)) ++text;
        if (!*text) break;
        const char *end=text; while (*end && !space((unsigned char)*end)) ++end;
        if (used==32 || budget() || intern(text,(size_t)(end-text),&local[used])) goto error;
        ++used; text=end;
    }
    if (!used) return CSS_OK;
    node *n=v;
    /* LibCSS releases string references, but the callback owns the array. */
    if (!n->css_classes) n->css_classes=malloc(32*sizeof(**result));
    *result=n->css_classes; if (!*result) goto error;
    memcpy(*result,local,used*sizeof(local[0])); *count=used; return CSS_OK;
error:
    while (used) lwc_string_unref(local[--used]);
    return CSS_NOMEM;
}
static css_error node_id(void *pw, void *v, lwc_string **result) {
    (void)pw; *result=NULL; const char *id=attr(v,"id");
    return id ? intern(id,strlen(id),result) : budget();
}
static css_error parent_cb(void *pw, void *v, void **out) { (void)pw; *out=parent(v); return budget(); }
static css_error sibling_cb(void *pw, void *v, void **out) { (void)pw; *out=sibling(v); return budget(); }
static css_error named_relation(void *v, const css_qname *q, void **out, unsigned mode) {
    node *n=v; *out=NULL;
    while ((n=mode<2 ? parent(n) : sibling(n))) {
        if (budget()) return CSS_NOMEM;
        if (qmatch(n,q)) { *out=n; break; }
        if (mode==1 || mode==3) break;
    }
    return CSS_OK;
}
#define RELATION(name, mode) static css_error name(void *pw,void *v,const css_qname *q,void **out) { (void)pw; return named_relation(v,q,out,mode); }
RELATION(ancestor_cb,0) RELATION(named_parent_cb,1) RELATION(generic_sibling_cb,2) RELATION(named_sibling_cb,3)
static css_error has_name(void *pw,void *v,const css_qname *q,bool *match) { (void)pw; *match=qmatch(v,q); return budget(); }
static int token_has(const char *s, lwc_string *value) {
    size_t n=lwc_string_length(value); if (!s || !n) return 0;
    while (*s) {
        if (budget()) return 0;
        while (space((unsigned char)*s)) ++s;
        const char *end=s; while (*end && !space((unsigned char)*end)) ++end;
        if ((size_t)(end-s)==n && !memcmp(s,lwc_string_data(value),n)) return 1;
        s=end;
    }
    return 0;
}
static css_error has_class(void *pw,void *v,lwc_string *value,bool *match) { (void)pw; *match=token_has(attr(v,"class"),value); return budget(); }
static css_error has_id(void *pw,void *v,lwc_string *value,bool *match) { (void)pw; *match=equal(attr(v,"id"),value,0); return budget(); }
static const char *qattr(node *n,const css_qname *q) {
    if (q->ns && lwc_string_length(q->ns) && !equal("*",q->ns,0)) return NULL;
    for (attribute *a=n->attributes;a;a=a->next) {
        if (budget()) return NULL;
        if (equal(a->name,q->name,1)) return a->value;
    }
    return NULL;
}
static css_error has_attribute(void *pw,void *v,const css_qname *q,bool *match) { (void)pw; *match=qattr(v,q)!=NULL; return budget(); }
static css_error match_attribute(void *v,const css_qname *q,lwc_string *value,bool *match,unsigned mode) {
    const char *s=qattr(v,q), *b=lwc_string_data(value); size_t n=lwc_string_length(value);
    *match=false; if (!s) return budget(); size_t length=strlen(s);
    if (!mode) *match=length==n && !memcmp(s,b,n);
    else if (n && mode==1) *match=length>=n && !memcmp(s,b,n) && (length==n || s[n]=='-');
    else if (n && mode==2) *match=token_has(s,value);
    else if (n && length>=n) {
        if (mode==3) *match=!memcmp(s,b,n);
        else if (mode==4) *match=!memcmp(s+length-n,b,n);
        else for (size_t i=0;i<=length-n;++i) {
            if (budget()) return CSS_NOMEM;
            if (!memcmp(s+i,b,n)) { *match=true; break; }
        }
    }
    return budget();
}
#define ATTRIBUTE(name, mode) static css_error name(void *pw,void *v,const css_qname *q,lwc_string *val,bool *match) { (void)pw; return match_attribute(v,q,val,match,mode); }
ATTRIBUTE(attr_equal,0) ATTRIBUTE(attr_dash,1) ATTRIBUTE(attr_includes,2)
ATTRIBUTE(attr_prefix,3) ATTRIBUTE(attr_suffix,4) ATTRIBUTE(attr_substring,5)
static css_error is_root(void *pw,void *v,bool *m) { (void)pw; *m=parent(v)==NULL; return budget(); }
static css_error count_siblings(void *pw,void *v,bool same,bool after,int32_t *count) {
    (void)pw; node *source=v; *count=0;
    for (node *n=after ? source->next : source->previous;n;n=after ? n->next : n->previous) {
        if (budget()) return CSS_NOMEM;
        if (n->kind==1 && (!same || !strcmp(n->name,source->name))) ++*count;
    }
    return CSS_OK;
}
static css_error is_empty(void *pw,void *v,bool *match) {
    (void)pw; *match=true;
    for (node *n=((node *)v)->first;n;n=n->next) {
        if (budget()) return CSS_NOMEM;
        if (n->kind==1 || (n->kind==3 && n->length)) { *match=false; break; }
    }
    return CSS_OK;
}
static css_error is_link(void *pw,void *v,bool *m) { (void)pw; *m=tag(v,"a") && attr(v,"href"); return budget(); }
/* Static document snapshot: no visited/history or active interaction state. */
static css_error no_state(void *pw,void *v,bool *m) { (void)pw; (void)v; *m=false; return budget(); }
static css_error is_disabled(void *pw,void *v,bool *m) { (void)pw; *m=(tag(v,"input") || tag(v,"button") || tag(v,"select") || tag(v,"textarea")) && attr(v,"disabled"); return budget(); }
static css_error is_enabled(void *pw,void *v,bool *m) { css_error rc=is_disabled(pw,v,m); *m=(tag(v,"input") || tag(v,"button") || tag(v,"select") || tag(v,"textarea")) && !*m; return rc; }
static css_error is_checked(void *pw,void *v,bool *m) { (void)pw; *m=(tag(v,"input") && attr(v,"checked")) || (tag(v,"option") && attr(v,"selected")); return budget(); }
static css_error is_lang(void *pw,void *v,lwc_string *lang,bool *m) {
    (void)pw; *m=false; size_t length=lwc_string_length(lang);
    for (node *n=v;n;n=parent(n)) {
        if (budget()) return CSS_NOMEM;
        const char *s=attr(n,"lang"); if (!s) continue;
        size_t size=strlen(s); *m=length && size>=length && !strncasecmp(s,lwc_string_data(lang),length) && (size==length || s[length]=='-'); break;
    }
    return CSS_OK;
}
static css_error hints(void *pw,void *v,uint32_t *count,css_hint **h) { (void)pw; (void)v; *count=0; *h=NULL; return budget(); }
static css_error ua_default(void *pw,uint32_t property,css_hint *h) {
    (void)pw; memset(h,0,sizeof(*h));
    if (property==CSS_PROP_COLOR) { h->status=CSS_COLOR_COLOR; h->data.color=0xff202020; }
    else if (property==CSS_PROP_FONT_FAMILY) h->status=CSS_FONT_FAMILY_MONOSPACE;
    else if (property==CSS_PROP_QUOTES) h->status=CSS_QUOTES_NONE;
    else if (property!=CSS_PROP_VOICE_FAMILY) return CSS_INVALID;
    return budget();
}
static css_error set_data(void *pw,void *v,void *data) { (void)pw; ((node *)v)->css_data=data; return CSS_OK; }
static css_error get_data(void *pw,void *v,void **data) { (void)pw; *data=((node *)v)->css_data; return budget(); }
static css_select_handler handler={CSS_SELECT_HANDLER_VERSION_1,node_name,node_classes,node_id,
    ancestor_cb,named_parent_cb,named_sibling_cb,generic_sibling_cb,parent_cb,sibling_cb,
    has_name,has_class,has_id,has_attribute,attr_equal,attr_dash,attr_includes,attr_prefix,attr_suffix,attr_substring,
    is_root,count_siblings,is_empty,is_link,no_state,no_state,no_state,no_state,is_enabled,is_disabled,is_checked,
    no_state,is_lang,hints,ua_default,set_data,get_data};
static css_error resolve_url(void *pw,const char *base,lwc_string *rel,lwc_string **out) {
    (void)pw; *out=NULL;
    size_t size=lwc_string_length(rel);
    if (budget() || memchr(lwc_string_data(rel),0,size)) return CSS_NOMEM;
    if(size>=BROWSER_RESOURCE_URL_CAPACITY) {
        /* Long data/absolute CSS URLs are opaque interned values, not fetch
         * requests. Import admission below still checks its own URL quota. */
        if(!document_profile || size>BROWSER_DOCUMENT_INPUT_CAPACITY) return CSS_NOMEM;
        const char *raw=lwc_string_data(rel);
        for(size_t i=0;i<size;++i) {
            if(raw[i]==':') return intern(raw,size,out);
            if(raw[i]=='/' || raw[i]=='?' || raw[i]=='#') break;
        }
        return intern("about:invalid#reist-url-limit",28,out);
    }
    static char reference[BROWSER_RESOURCE_URL_CAPACITY], absolute[BROWSER_RESOURCE_URL_CAPACITY];
    static reist_html_url_workspace_t resolver;
    memcpy(reference,lwc_string_data(rel),size); reference[size]=0;
    /* Pure URI handling, never fetching. Already absolute references remain
     * opaque: CSS URL resources have no consumer in this scene revision. */
    for (size_t i=0;i<size;++i) {
        if (reference[i]==':') return intern(reference,size,out);
        if (reference[i]=='/' || reference[i]=='?' || reference[i]=='#') break;
    }
    if (reist_html_url_resolve_wide(base,reference,absolute,sizeof(absolute),&resolver)) return CSS_BADPARM;
    return intern(absolute,strlen(absolute),out);
}
static int make_sheet_at(const char *bytes,size_t length,bool inlined,const char *url,uint32_t depth,css_stylesheet **result) {
    *result=NULL;
    if(depth>BROWSER_RESOURCE_DEPTH || budget()) return -28;
    import_path[depth]=url;
    css_stylesheet_params p={.params_version=CSS_STYLESHEET_PARAMS_VERSION_1,.level=CSS_LEVEL_21,
        .charset="UTF-8",.url=url,.inline_style=inlined,.resolve=resolve_url};
    css_error rc=css_stylesheet_create(&p,result);
    if (rc==CSS_OK) rc=css_stylesheet_append_data(*result,(const uint8_t *)bytes,length);
    if (rc==CSS_OK || rc==CSS_NEEDDATA) rc=css_stylesheet_data_done(*result);
    if(rc==CSS_IMPORTS_PENDING && resources) {
        for (;;) {
            lwc_string *relative=NULL;
            rc=css_stylesheet_next_pending_import(*result,&relative);
            /* LibCSS specifies CSS_INVALID when no pending import remains. */
            if(rc==CSS_INVALID) { rc=CSS_OK; break; }
            if(rc!=CSS_OK) break;
            char *reference=import_reference[depth], *canonical=import_canonical[depth];
            size_t n=lwc_string_length(relative);
            if(n>=BROWSER_RESOURCE_URL_CAPACITY || memchr(lwc_string_data(relative),0,n)) {
                lwc_string_unref(relative); rc=CSS_INVALID; break;
            }
            memcpy(reference,lwc_string_data(relative),n); reference[n]=0; lwc_string_unref(relative);
            if(browser_resource_url(url,reference,canonical) || browser_resource_admit(document_url,canonical) || budget()) { rc=CSS_INVALID; break; }
            unsigned cycle=0;
            int index=browser_resources_find(resources,canonical);
            const browser_resource_t *entry=index>=0 ? &resources->entries[index] : NULL;
            for(uint32_t i=0;i<=depth;++i) if(!strcmp(import_path[i],canonical) ||
                (entry && entry->ready && !strcmp(import_path[i],entry->effective))) cycle=1;
            if(!cycle && depth==BROWSER_RESOURCE_DEPTH) { rc=CSS_NOMEM; break; }
            if(!cycle && (!entry || !entry->ready) && browser_resource_need_add(resource_needs,canonical,depth+1)) { rc=CSS_NOMEM; break; }
            css_stylesheet *child=NULL;
            if(imported_count==128) { rc=CSS_NOMEM; break; }
            /* Empty pending/cyclic imports are only discovery placeholders.
             * A scene is never emitted while any required bytes are missing. */
            int bad=make_sheet_at(cycle || !entry || !entry->ready ? "" : (const char *)resources->bytes+entry->offset,
                cycle || !entry || !entry->ready ? 0 : entry->length,false,
                entry && entry->ready ? entry->effective : canonical,
                cycle ? depth : depth+1,&child);
            import_path[depth]=url;
            if(bad || imported_count==128) { if(child) css_stylesheet_destroy(child); rc=CSS_NOMEM; break; }
            imported[imported_count++]=child;
            if(css_stylesheet_register_import(*result,child)!=CSS_OK) { rc=CSS_INVALID; break; }
        }
    }
    if (rc!=CSS_OK) {
        if (*result) css_stylesheet_destroy(*result); *result=NULL; return -28;
    }
    return 0;
}
static int make_sheet(const char *bytes,size_t length,bool inlined,css_stylesheet **result) {
    return make_sheet_at(bytes,length,inlined,document_url,0,result);
}
static int add_sheet(const char *bytes,size_t length,css_origin origin,const char *media_name) {
    if (sheet_count==CSS_SHEETS || budget()) return -28;
    css_stylesheet *sheet;
    if (make_sheet(bytes,length,false,&sheet)) return -28;
    sheets[sheet_count++]=sheet;
    return css_select_ctx_append_sheet(context,sheet,origin,media_name)==CSS_OK ? 0 : -28;
}
static int collect_sheets(node *n,uint32_t depth) {
    if (depth>=128 || budget()) return -28;
    if (tag(n,"style")) {
        const char *type=attr(n,"type");
        if (type && *type && strncasecmp(type,"text/css",9)) return 0;
        size_t length=0;
        for (node *c=n->first;c;c=c->next) if (c->kind==3) {
            if (budget() || c->length>sizeof(sheet_bytes)-length) return -28;
            memcpy(sheet_bytes+length,c->text,c->length); length+=c->length;
        }
        return add_sheet(sheet_bytes,length,CSS_ORIGIN_AUTHOR,attr(n,"media"));
    }
    if(resources && tag(n,"link")) {
        const char *rel=attr(n,"rel"), *href=attr(n,"href"), *type=attr(n,"type");
        unsigned style=0, alternate=0;
        if(rel) for(size_t i=0;rel[i];) {
            while(space((uint8_t)rel[i])) ++i;
            size_t start=i; while(rel[i] && !space((uint8_t)rel[i])) ++i;
            if(i-start==10 && !strncasecmp(rel+start,"stylesheet",10)) style=1;
            if(i-start==9 && !strncasecmp(rel+start,"alternate",9)) alternate=1;
        }
        if(!style || alternate || !href || attr(n,"disabled") || (type && *type && strncasecmp(type,"text/css",9))) return 0;
        static char canonical[BROWSER_RESOURCE_URL_CAPACITY];
        if(browser_resource_url(document_url,href,canonical) || browser_resource_admit(document_url,canonical)) return -13;
        int index=browser_resources_find(resources,canonical);
        if(index<0 || !resources->entries[index].ready) return browser_resource_need_add(resource_needs,canonical,0);
        const browser_resource_t *entry=&resources->entries[index];
        if(sheet_count==CSS_SHEETS) return -28;
        css_stylesheet *sheet;
        if(make_sheet_at((const char *)resources->bytes+entry->offset,entry->length,false,entry->effective,0,&sheet)) return -28;
        sheets[sheet_count++]=sheet;
        return css_select_ctx_append_sheet(context,sheet,CSS_ORIGIN_AUTHOR,attr(n,"media"))==CSS_OK ? 0 : -28;
    }
    for (node *c=n->first;c;c=c->next) if (collect_sheets(c,depth+1)) return -28;
    return 0;
}
static int select_tree(node *n,css_computed_style *inherited,uint32_t depth) {
    if (depth>=128 || budget()) return -28;
    if (n->kind==1) {
        if (n->ns!=HUBBUB_NS_HTML) return 0;
        css_stylesheet *inlined=NULL; const char *style=attr(n,"style");
        if (style && make_sheet(style,strlen(style),true,&inlined)) return -28;
        css_select_results *selected=NULL;
        if(browser_css_values_begin((browser_css_values **)&n->css_values,
                n->parent ? n->parent->css_values : NULL)) {
            if(inlined) css_stylesheet_destroy(inlined);
            return -28;
        }
        css_error rc=css_select_style(context,n,&units,&media,inlined,&handler,NULL,&selected);
        if(browser_css_values_collecting()) {
            if(rc==CSS_OK && browser_css_values_resolve()) rc=CSS_NOMEM;
            if(selected) { css_select_results_destroy(selected); selected=NULL; }
            if(n->css_data) {
                css_libcss_node_data_handler(&handler,CSS_NODE_DELETED,NULL,n,NULL,n->css_data);
                n->css_data=NULL;
            }
            free(n->css_classes); n->css_classes=NULL;
            if(rc==CSS_OK) rc=css_select_style(context,n,&units,&media,inlined,&handler,NULL,&selected);
        }
        if (inlined) css_stylesheet_destroy(inlined);
        if (rc==CSS_OK) {
            if (inherited) rc=css_computed_style_compose(inherited,selected->styles[0],&units,(css_computed_style **)&n->css_style);
            else { n->css_style=selected->styles[0]; selected->styles[0]=NULL; units.root_style=n->css_style; }
        }
        if (selected) css_select_results_destroy(selected);
        if (rc!=CSS_OK || !n->css_style) return -28;
        inherited=n->css_style;
    }
    for (node *c=n->first;c;c=c->next) if (select_tree(c,inherited,depth+1)) return -28;
    return 0;
}
static void cleanup_tree(node *n) {
    /* The admitted extended HTML tree has <=8192 nodes and depth <128. No allocations. */
    for (node *c=n->first;c;c=c->next) cleanup_tree(c);
    if (n->css_data) css_libcss_node_data_handler(&handler,CSS_NODE_DELETED,NULL,n,NULL,n->css_data);
    if (n->css_style) css_computed_style_destroy(n->css_style);
    browser_css_values_destroy(n->css_values); n->css_values=NULL;
    free(n->css_classes); n->css_classes=NULL;
    n->css_style=n->css_data=NULL;
}
typedef struct flow {
    int32_t left,right,x,y,line,margin;
    uint32_t line_start,align;
    int pending_space,content;
    node *forced_node;
    int32_t forced_width,forced_height;
} flow;
static int32_t length_px(css_computed_style *s,css_fixed value,css_unit unit,int32_t reference) {
    int64_t result=unit==CSS_UNIT_PCT ? (int64_t)value*reference/(100*1024) :
        css_unit_len2css_px(s,&units,value,unit)/1024;
    if (result<-BROWSER_SCENE_COORD_LIMIT || result>BROWSER_SCENE_COORD_LIMIT) { failed=1; return 0; }
    return (int32_t)result;
}
static int emit(uint32_t kind,uint32_t offset,uint32_t length,uint32_t link,int32_t x,int32_t y,
                uint32_t width,uint32_t height,uint32_t color,uint32_t flags) {
    if (budget() || scene->count==BROWSER_SCENE_RUNS || x<-BROWSER_SCENE_COORD_LIMIT ||
        x>BROWSER_SCENE_COORD_LIMIT || y<-BROWSER_SCENE_COORD_LIMIT || y>BROWSER_SCENE_COORD_LIMIT ||
        width>BROWSER_SCENE_COORD_LIMIT || height>BROWSER_SCENE_COORD_LIMIT) return -28;
    if (kind==1 && scene->count) {
        browser_scene_run_t *r=&scene->runs[scene->count-1];
        if (r->kind==1 && r->y==y && r->x+(int32_t)r->width==x && r->height==height &&
            r->link==link && r->color==color && r->flags==flags && r->offset+r->length==offset && r->length+length<=128) {
            r->width+=width; r->length+=length; return 0;
        }
    }
    scene->runs[scene->count++]=(browser_scene_run_t){kind,offset,length,link,x,y,width,height,color,flags};
    if (y>=0 && (uint32_t)y+height>scene->total_height) scene->total_height=(uint32_t)y+height;
    return 0;
}
static void end_line(flow *f,int force) {
    if (!f->content && !force) return;
    int32_t gap=f->right-f->x, shift=f->align==CSS_TEXT_ALIGN_CENTER ? gap/2 : f->align==CSS_TEXT_ALIGN_RIGHT ? gap : 0;
    if (shift>0) for (uint32_t i=f->line_start;i<scene->count;++i) scene->runs[i].x+=shift;
    f->y+=f->line ? f->line : 18; f->x=f->left; f->line=0; f->content=0;
    f->pending_space=0; f->line_start=scene->count;
}
static int copy_field(char *out,size_t capacity,const char *text) {
    if (!text) { *out=0; return 0; } size_t n=strlen(text);
    if (n>=capacity) return -28;
    memcpy(out,text,n+1); return 0;
}
static uint32_t scalar_size(const char *s,size_t left) {
    uint8_t c=(uint8_t)*s; uint32_t n=c<0x80 ? 1 : c<0xe0 ? 2 : c<0xf0 ? 3 : 4;
    return n<=left ? n : (uint32_t)left;
}
/* HTML size/cols/rows affect intrinsic native geometry, never input quotas.
 * Saturate at the viewport-scale bound while accounting every scanned byte. */
static uint32_t control_size(node *n,const char *name,uint32_t fallback,uint32_t maximum) {
    const char *p=attr(n,name); if(!p) return fallback;
    while(space((uint8_t)*p)) { if(budget()) return fallback; ++p; }
    if(*p=='+') ++p;
    if(*p<'0' || *p>'9') return fallback;
    uint32_t value=0;
    while(*p>='0' && *p<='9') {
        if(budget()) return fallback;
        uint32_t digit=(uint32_t)(*p++-'0');
        value=value>(maximum-digit)/10U ? maximum : value*10U+digit;
    }
    return value ? value : fallback;
}
static uint32_t control_cells(const char *text) {
    uint32_t cells=0;
    for(uint32_t i=0;text[i] && cells<128;++i) {
        if(budget()) return cells;
        if(((uint8_t)text[i]&192)!=128) ++cells;
    }
    return cells;
}
static uint32_t font_scalar(const char *s,size_t left) {
    uint32_t n=scalar_size(s,left),v=(uint8_t)*s;
    if(n>1) { v&=(1U<<(7-n))-1;for(uint32_t i=1;i<n;++i)v=(v<<6)|((uint8_t)s[i]&63U); }
    return v;
}
static uint32_t font_face(css_computed_style *s) {
    if(!document_profile)return 0;
    lwc_string **names=NULL;uint8_t generic=css_computed_font_family(s,&names);uint32_t family=0;
    for(uint32_t i=0;names && names[i];++i) {
        if(i==256 || budget()) { failed=1;return 0; }
        const char *name=lwc_string_data(names[i]);size_t n=lwc_string_length(names[i]);
        if(n==16 && !strncasecmp(name,"Liberation Serif",n)) {family=1;break;}
        if(n==15 && !strncasecmp(name,"Liberation Sans",n)) {family=5;break;}
    }
    if(!family)family=generic==CSS_FONT_FAMILY_SERIF ? 1 : generic==CSS_FONT_FAMILY_SANS_SERIF ? 5 : 0;
    if(!family)return 0;
    uint32_t weight=css_computed_font_weight(s);
    if(weight==CSS_FONT_WEIGHT_BOLD || weight==CSS_FONT_WEIGHT_BOLDER || weight>=CSS_FONT_WEIGHT_600)++family;
    if(css_computed_font_style(s)!=CSS_FONT_STYLE_NORMAL)family+=2;
    return family;
}
static uint32_t glyph_advance(uint32_t face,uint32_t height,uint32_t scalar,uint32_t *flags) {
    if(flags)*flags&=~(BROWSER_FONT_FLAG|BROWSER_FONT_FACE_MASK);
    if(face) {
        const browser_font_glyph *glyph=NULL;int rc=browser_font_get(face,height,scalar,&glyph);
        if(rc<0) { failed=1;return 0; }
        if(!rc) {
            if(flags)*flags=(*flags&~3U)|BROWSER_FONT_FLAG|(face<<BROWSER_FONT_FACE_SHIFT);
            return (uint32_t)glyph->advance;
        }
    }
    return (height+1)/2;
}
static int append_text(flow *f,css_computed_style *s,const char *text,size_t length,uint32_t link) {
    css_fixed fixed; css_unit unit; css_computed_font_size(s,&fixed,&unit);
    int32_t font=length_px(s,fixed,unit,16);
    if (font<1 || font>(int32_t)BROWSER_CSS_FONT_MAX) return -28;
    uint32_t face=font_face(s),cell=glyph_advance(face,(uint32_t)font,' ',NULL),color;
    css_computed_color(s,&color);
    uint32_t weight=css_computed_font_weight(s), flags=0;
    if (weight==CSS_FONT_WEIGHT_BOLD || weight==CSS_FONT_WEIGHT_BOLDER || weight>=CSS_FONT_WEIGHT_600) flags|=1;
    if (css_computed_font_style(s)!=CSS_FONT_STYLE_NORMAL) flags|=2;
    if (link!=UINT32_MAX) flags|=REIST_HTML_STYLE_LINK;
    if(document_profile && link!=UINT32_MAX && !(css_computed_text_decoration(s)&CSS_TEXT_DECORATION_UNDERLINE)) {
        flags|=BROWSER_SCENE_NO_UNDERLINE; scene->version=BROWSER_SCENE_LAYOUT_VERSION;
    }
    int pre=css_computed_white_space(s)==CSS_WHITE_SPACE_PRE || css_computed_white_space(s)==CSS_WHITE_SPACE_PRE_WRAP;
    uint8_t line_type=css_computed_line_height(s,&fixed,&unit);
    int32_t line=line_type==CSS_LINE_HEIGHT_NUMBER ? (int32_t)((int64_t)fixed*font/1024) :
        line_type==CSS_LINE_HEIGHT_DIMENSION ? length_px(s,fixed,unit,font) : font+2;
    if (line<font) line=font;
    for (size_t i=0;i<length;) {
        if (budget()) return -28;
        if (!pre && space((uint8_t)text[i])) { f->pending_space=1; ++i; continue; }
        if (pre && text[i]=='\n') { end_line(f,1); ++i; continue; }
        if (!pre && (i==0 || space((uint8_t)text[i-1]))) {
            uint32_t width=0;
            for (size_t j=i;j<length && !space((uint8_t)text[j]);j+=scalar_size(text+j,length-j)) {
                if (budget()) return -28; width+=glyph_advance(face,(uint32_t)font,font_scalar(text+j,length-j),NULL);
            }
            if ((int32_t)width<=f->right-f->left && f->content && f->x+(int32_t)width+(f->pending_space ? (int32_t)cell : 0)>f->right) end_line(f,0);
        }
        if (f->pending_space && f->content) {
            if (f->x+(int32_t)cell>f->right) end_line(f,0);
            else {
                if (doc->text_length==sizeof(doc->text)) return -28;
                uint32_t at=doc->text_length; doc->text[doc->text_length++]=' ';
                uint32_t glyph_flags=flags;glyph_advance(face,(uint32_t)font,' ',&glyph_flags);
                if (emit(1,at,1,link,f->x,f->y,cell,(uint32_t)font,color,glyph_flags)) return -28;
                f->x+=(int32_t)cell;
            }
        }
        f->pending_space=0;
        uint32_t n=scalar_size(text+i,length-i);
        uint32_t glyph_flags=flags,advance=glyph_advance(face,(uint32_t)font,font_scalar(text+i,length-i),&glyph_flags);
        if (doc->text_length+n>sizeof(doc->text)) return -28;
        if (f->content && f->x+(int32_t)advance>f->right) end_line(f,0);
        uint32_t at=doc->text_length;
        memcpy(doc->text+at,text+i,n); doc->text_length+=n;
        if (emit(1,at,n,link,f->x,f->y,advance,(uint32_t)font,color,glyph_flags)) return -28;
        f->x+=(int32_t)advance; if (line>f->line) f->line=line;
        f->content=1; i+=n;
    }
    return failed ? -28 : 0;
}
typedef uint8_t (*dimension_fn)(const css_computed_style *,css_fixed *,css_unit *);
static int32_t dimension(css_computed_style *s,dimension_fn fn,int32_t reference,int *automatic) {
    css_fixed fixed=0; css_unit unit=CSS_UNIT_PX; uint8_t type=fn(s,&fixed,&unit);
    if (automatic) *automatic=type==CSS_WIDTH_AUTO;
    return type==CSS_WIDTH_AUTO ? 0 : length_px(s,fixed,unit,reference);
}
static int32_t collapse(int32_t a,int32_t b) {
    if (a>=0 && b>=0) return a>b ? a : b;
    if (a<=0 && b<=0) return a<b ? a : b;
    return a+b;
}
static int layout_node(node *,css_computed_style *,flow *,uint32_t,uint32_t,int32_t);
static int layout_container(node *,css_computed_style *,flow *,uint32_t,uint32_t,int32_t);
static int32_t intrinsic_width(node *,css_computed_style *,int32_t,uint32_t,int);
static int32_t extra_length(css_computed_style *,browser_css_length,int32_t);
static int children(node *n,css_computed_style *s,flow *f,uint32_t link,uint32_t depth,int32_t definite_height) {
    for (node *c=n->first;c;c=c->next) if (layout_node(c,s,f,link,depth+1,definite_height)) return -28;
    return 0;
}
static int layout_node(node *n,css_computed_style *inherited,flow *outer,uint32_t link,uint32_t depth,int32_t containing_height) {
    if (depth>=128 || budget()) return -28;
    if (n->kind==3) return inherited ? append_text(outer,inherited,n->text,n->length,link) : 0;
    if (n->kind==9) return children(n,inherited,outer,link,depth,containing_height);
    if (n->kind!=1 || n->ns!=HUBBUB_NS_HTML || !n->css_style) return 0;
    css_computed_style *s=n->css_style;
    uint8_t display=css_computed_display_static(s);
    if (display==CSS_DISPLAY_NONE) return 0;
    /* Empty absolute clip rectangles are commonly used for accessible helper
     * text. They neither paint nor occupy normal flow. No class-name heuristic. */
    if(css_computed_position(s)==CSS_POSITION_ABSOLUTE || css_computed_position(s)==CSS_POSITION_FIXED) {
        css_computed_clip_rect clip;
        if(css_computed_clip(s,&clip)==CSS_CLIP_RECT && !clip.top_auto && !clip.right_auto && !clip.bottom_auto && !clip.left_auto &&
            (length_px(s,clip.right,clip.runit,outer->right-outer->left)<=length_px(s,clip.left,clip.lunit,outer->right-outer->left) ||
             length_px(s,clip.bottom,clip.bunit,containing_height)<=length_px(s,clip.top,clip.tunit,containing_height))) return failed ? -28 : 0;
    }
    if (tag(n,"br")) { end_line(outer,1); return 0; }
    const char *href=tag(n,"a") ? attr(n,"href") : NULL;
    if(href && document_profile && strlen(href)>=sizeof(doc->links[0].href)) {
        href=NULL; link=UINT32_MAX; /* Visible but inert; never navigate a truncated URL. */
    }
    if (href) {
        if (doc->link_count==REIST_HTML_LINK_CAPACITY || copy_field(doc->links[doc->link_count].href,256,href)) return -28;
        link=doc->link_count++;
    }
    int forced=n==outer->forced_node;
    int inline_box=!forced && (display==CSS_DISPLAY_INLINE_BLOCK || display==CSS_DISPLAY_INLINE_FLEX);
    int block=forced || inline_box || display!=CSS_DISPLAY_INLINE;
    int native=n->control_index && scene->forms.controls[n->control_index-1].kind!=BROWSER_FORM_LABEL;
    if(native && scene->forms.controls[n->control_index-1].kind==BROWSER_FORM_HIDDEN) return 0;
    int control_block=native && block;
    /* A native control is one replaced box. Generic block decorations would
     * paint an unrelated full-width rectangle behind its intrinsic widget. */
    if(native) { if(control_block) end_line(outer,0); block=0; }
    flow local=*outer, *f=outer;
    int32_t margins[4]={0},pads[4]={0},borders[4]={0},width=0,box_x=0,box_y=0,explicit_height=0;
    uint32_t border_colors[4]={0}, background=0, fill_index=UINT32_MAX;
    uint32_t border_index[4]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    const browser_css_extra *decoration=browser_css_values_extra(n->css_values);
    int32_t radius=extra_length(s,decoration->radius,outer->right-outer->left);
    uint32_t shadow_index=UINT32_MAX,round_border=UINT32_MAX;
    if(radius<0) return -28;
    if(radius>64) radius=64; /* Used radius is further limited by the box. */
    int width_auto=1,height_auto=1;
    if (block) {
        if(!inline_box) end_line(outer,0);
        dimension_fn margin_fn[]={css_computed_margin_top,css_computed_margin_right,css_computed_margin_bottom,css_computed_margin_left};
        dimension_fn padding_fn[]={css_computed_padding_top,css_computed_padding_right,css_computed_padding_bottom,css_computed_padding_left};
        dimension_fn border_fn[]={css_computed_border_top_width,css_computed_border_right_width,css_computed_border_bottom_width,css_computed_border_left_width};
        uint8_t border_style[]={css_computed_border_top_style(s),css_computed_border_right_style(s),css_computed_border_bottom_style(s),css_computed_border_left_style(s)};
        uint8_t border_types[]={css_computed_border_top_color(s,&border_colors[0]),css_computed_border_right_color(s,&border_colors[1]),css_computed_border_bottom_color(s,&border_colors[2]),css_computed_border_left_color(s,&border_colors[3])};
        int32_t available=outer->right-outer->left; int auto_left=0,auto_right=0;
        for (unsigned i=0;i<4;++i) {
            int auto_margin=0; margins[i]=dimension(s,margin_fn[i],available,&auto_margin);
            if (i==1) auto_right=auto_margin; if (i==3) auto_left=auto_margin;
            pads[i]=dimension(s,padding_fn[i],available,NULL);
            if (border_style[i]!=CSS_BORDER_STYLE_NONE && border_style[i]!=CSS_BORDER_STYLE_HIDDEN) {
                css_fixed value; css_unit unit; uint8_t kind=border_fn[i](s,&value,&unit);
                borders[i]=kind==CSS_BORDER_WIDTH_THIN ? 1 : kind==CSS_BORDER_WIDTH_MEDIUM ? 2 :
                    kind==CSS_BORDER_WIDTH_THICK ? 4 : length_px(s,value,unit,available);
            }
            if (pads[i]<0 || borders[i]<0) return -28;
            if (border_types[i]==CSS_BORDER_COLOR_CURRENT_COLOR) css_computed_color(s,&border_colors[i]);
        }
        width=dimension(s,css_computed_width,available,&width_auto);
        int32_t extra=pads[1]+pads[3]+borders[1]+borders[3];
        if(forced) { memset(margins,0,sizeof(margins)); auto_left=auto_right=0; }
        if (width_auto) width=available-margins[1]-margins[3]-extra;
        if(inline_box && width_auto) width=intrinsic_width(n,s,available,depth,0)-extra;
        int32_t minimum=dimension(s,css_computed_min_width,available,NULL);
        int max_auto=0; int32_t maximum=dimension(s,css_computed_max_width,available,&max_auto);
        int border_box=css_computed_box_sizing(s)==CSS_BOX_SIZING_BORDER_BOX;
        if(width<0) width=0;
        if(width_auto && border_box) width+=extra;
        if(browser_css_box_size(width,minimum,max_auto ? BROWSER_SCENE_COORD_LIMIT : maximum,extra,border_box,&width)) return -28;
        if(forced) { if(outer->forced_width<extra) width=0; else width=outer->forced_width-extra; }
        if(inline_box && outer->content && outer->x+width+extra+margins[1]+margins[3]>outer->right) end_line(outer,0);
        {
            int32_t free=available-width-extra-margins[1]-margins[3];
            if (free>0 && (auto_left || auto_right)) {
                if (auto_left) margins[3]=auto_right ? free/2 : free;
                if (auto_right) margins[1]=auto_left ? free-free/2 : free;
            }
        }
        if (width<0) width=0;
        explicit_height=dimension(s,css_computed_height,containing_height,&height_auto);
        css_fixed height_value=0; css_unit height_unit=CSS_UNIT_PX;
        css_computed_height(s,&height_value,&height_unit);
        if (height_unit==CSS_UNIT_PCT && !containing_height) height_auto=1;
        if(forced && outer->forced_height>=0) {
            explicit_height=outer->forced_height-pads[0]-pads[2]-borders[0]-borders[2];
            if(explicit_height<0) explicit_height=0;
            /* This override is already a content-box used height. */
            if(border_box) explicit_height+=pads[0]+pads[2]+borders[0]+borders[2];
            height_auto=0;
        }
        if (explicit_height<0) return -28;
        box_x=(inline_box ? outer->x : outer->left)+margins[3];
        box_y=outer->y+(inline_box ? margins[0] : collapse(outer->margin,margins[0]));
        local=(flow){.left=box_x+pads[3]+borders[3],.right=box_x+pads[3]+borders[3]+width,
            .x=box_x+pads[3]+borders[3],.y=box_y+pads[0]+borders[0],.align=css_computed_text_align(s)};
        f=&local;
        css_computed_background_color(s,&background);
        /* Reserve block decorations before descendants to retain paint order. */
        if(document_profile && decoration->shadow_color) {
            scene->version=BROWSER_SCENE_LAYOUT_VERSION; shadow_index=scene->count;
            if(emit(BROWSER_SCENE_SHADOW,(uint32_t)radius,(uint32_t)decoration->shadow_blur,UINT32_MAX,box_x,box_y,0,0,decoration->shadow_color,0)) return -28;
        }
        if ((background>>24) || (document_profile && link!=UINT32_MAX)) {
            fill_index=scene->count;
            int rounded=document_profile && (radius || link!=UINT32_MAX);
            if(rounded) scene->version=BROWSER_SCENE_LAYOUT_VERSION;
            if (emit(rounded ? BROWSER_SCENE_ROUND : BROWSER_SCENE_FILL,rounded ? (uint32_t)radius : 0,0,
                    rounded ? link : UINT32_MAX,box_x,box_y,(uint32_t)(width+extra),0,background,
                    rounded && link!=UINT32_MAX ? 64 : 0)) return -28;
        }
        int uniform=document_profile && radius && borders[0] && borders[0]<=32;
        for(unsigned i=1;i<4;++i) if(borders[i]!=borders[0] || border_colors[i]!=border_colors[0]) uniform=0;
        if(uniform) {
            scene->version=BROWSER_SCENE_LAYOUT_VERSION; round_border=scene->count;
            if(emit(BROWSER_SCENE_ROUND,(uint32_t)radius,(uint32_t)borders[0],UINT32_MAX,box_x,box_y,(uint32_t)(width+extra),0,border_colors[0],0)) return -28;
        }
        for (unsigned i=0;i<4;++i) if (borders[i] && !uniform) {
            border_index[i]=scene->count;
            if (emit(BROWSER_SCENE_FILL,0,0,UINT32_MAX,box_x,box_y,0,0,border_colors[i],0)) return -28;
        }
        f->line_start=scene->count;
    }
    const char *id=attr(n,"id"); if (!id && tag(n,"a")) id=attr(n,"name");
    if (id && *id) {
        if (doc->anchor_count==REIST_HTML_ANCHOR_CAPACITY || copy_field(doc->anchors[doc->anchor_count].name,128,id)) return -28;
        if (emit(6,doc->anchor_count++,0,UINT32_MAX,f->x,f->y,0,0,0,0)) return -28;
    }
    if (n->control_index && scene->forms.controls[n->control_index-1].kind!=BROWSER_FORM_LABEL) {
        uint32_t index=n->control_index-1;
        const browser_form_control_t *c=&scene->forms.controls[index];
        if(c->kind!=BROWSER_FORM_HIDDEN) {
            int aw=1,ah=1; int32_t w=dimension(s,css_computed_width,f->right-f->left,&aw);
            int32_t h=dimension(s,css_computed_height,containing_height,&ah);
            int toggle=c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO;
            int button=c->kind==BROWSER_FORM_SUBMIT || c->kind==BROWSER_FORM_RESET || c->kind==BROWSER_FORM_BUTTON;
            if(aw) {
                if(toggle) w=18;
                else if(button) w=(int32_t)control_cells(scene->forms.strings+c->label)*8+16;
                else if(c->kind==BROWSER_FORM_SELECT) {
                    uint32_t cells=1;
                    for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j) {
                        uint32_t count=control_cells(scene->forms.strings+scene->forms.options[j].label);
                        if(count>cells) cells=count;
                    }
                    w=(int32_t)cells*8+24;
                } else w=(int32_t)control_size(n,c->kind==BROWSER_FORM_TEXTAREA ? "cols" : "size",20,127)*8+8;
            }
            if(ah) h=toggle ? 18 : c->kind==BROWSER_FORM_TEXTAREA ? (int32_t)control_size(n,"rows",2,47)*16+8 : 24;
            if(w>f->right-f->left) w=f->right-f->left;
            if(w<0 || h<0 || w>1024 || h>768) return -28;
            if(w && h) {
                if(f->content && f->x+w>f->right) end_line(f,0);
                if(emit(BROWSER_SCENE_CONTROL,index,0,UINT32_MAX,f->x,f->y,(uint32_t)w,(uint32_t)h,0,0)) return -28;
                f->x+=w+4; if(h+2>f->line) f->line=h+2; f->content=1;
                if(control_block) end_line(f,0);
            }
        }
    } else if (tag(n,"img")) {
        if (doc->image_count==16) return -28;
        uint32_t index=doc->image_count++;
        reist_html_image_t *image=&doc->images[index];
        const char *source=attr(n,"src"),*alt=attr(n,"alt");
        if(document_profile && source && strlen(source)>=sizeof(image->source)) {
            if(copy_field(scene->image_urls[index],BROWSER_RESOURCE_URL_CAPACITY,source)) return -28;
            source=""; /* Full address is carried by private scene v4. */
        }
        if(document_profile && alt && strlen(alt)>=sizeof(image->alt)) alt="[image]";
        if (copy_field(image->source,256,source) || copy_field(image->alt,128,alt)) return -28;
        int aw=1,ah=1; int32_t w=dimension(s,css_computed_width,f->right-f->left,&aw);
        int32_t h=dimension(s,css_computed_height,containing_height,&ah);
        const char *dimensions[]={attr(n,"width"),attr(n,"height")}; uint32_t values[2]={0};
        for (unsigned i=0;i<2;++i) if (dimensions[i]) {
            for (const char *p=dimensions[i];*p>='0' && *p<='9';++p) { if (values[i]>1024) return -28; values[i]=values[i]*10+(uint32_t)(*p-'0'); }
            if (values[i]>1024) return -28;
        }
        image->width=(uint16_t)values[0]; image->height=(uint16_t)values[1];
        uint32_t sw=intrinsic && intrinsic[index][0] ? intrinsic[index][0] : 160;
        uint32_t sh=intrinsic && intrinsic[index][1] ? intrinsic[index][1] : 48;
        if (aw) w=values[0] ? (int32_t)values[0] : (int32_t)sw;
        if (ah) h=values[1] ? (int32_t)values[1] : (int32_t)sh;
        if ((!aw || values[0]) && ah && !values[1]) h=(int32_t)((int64_t)sh*w/sw);
        if ((!ah || values[1]) && aw && !values[0]) w=(int32_t)((int64_t)sw*h/sh);
        if (w>f->right-f->left && w>0) { h=(int32_t)((int64_t)h*(f->right-f->left)/w); w=f->right-f->left; }
        if (w<0 || h<0) return -28;
        if (w && h) {
            if (f->content && f->x+w>f->right) end_line(f,0);
            if (emit(5,index,0,link,f->x,f->y,(uint32_t)w,(uint32_t)h,0,link!=UINT32_MAX ? 64 : 0)) return -28;
            f->x+=w; if (h+2>f->line) f->line=h+2; f->content=1;
        }
    } else {
        int32_t label_x=f->x,label_y=f->y; uint32_t label_start=scene->count;
        int32_t definite=height_auto ? 0 : explicit_height;
        if(!height_auto && css_computed_box_sizing(s)==CSS_BOX_SIZING_BORDER_BOX) {
            definite-=pads[0]+pads[2]+borders[0]+borders[2]; if(definite<0) definite=0;
        }
        if(display==CSS_DISPLAY_FLEX || display==CSS_DISPLAY_INLINE_FLEX || browser_css_values_extra(n->css_values)->grid) {
            if(layout_container(n,s,f,link,depth,definite)) return -28;
        } else if(children(n,s,f,link,depth,definite)) return -28;
        if(n->control_index && scene->count>label_start) {
            int32_t w=f->y==label_y ? f->x-label_x : f->right-label_x;
            if(w>0 && w<=1024 && emit(BROWSER_SCENE_CONTROL,n->control_index-1,0,UINT32_MAX,label_x,label_y,(uint32_t)w,18,0,0)) return -28;
        }
    }
    if (block) {
        end_line(f,0);
        int32_t content_height=f->y-(box_y+pads[0]+borders[0])+f->margin;
        if (!height_auto) content_height=explicit_height;
        if (content_height<0) content_height=0;
        int max_auto=0;
        int32_t minimum=dimension(s,css_computed_min_height,containing_height,NULL);
        int32_t maximum=dimension(s,css_computed_max_height,containing_height,&max_auto);
        int32_t extra=pads[0]+pads[2]+borders[0]+borders[2];
        int border_box=css_computed_box_sizing(s)==CSS_BOX_SIZING_BORDER_BOX;
        if(height_auto && border_box) content_height+=extra;
        if(browser_css_box_size(content_height,minimum,max_auto ? BROWSER_SCENE_COORD_LIMIT : maximum,extra,border_box,&content_height)) return -28;
        int32_t box_height=content_height+pads[0]+pads[2]+borders[0]+borders[2];
        uint32_t box_width=(uint32_t)(width+pads[1]+pads[3]+borders[1]+borders[3]);
        if(radius>(int32_t)box_width/2) radius=(int32_t)box_width/2;
        if(radius>box_height/2) radius=box_height/2;
        if (fill_index!=UINT32_MAX) scene->runs[fill_index].height=(uint32_t)box_height;
        if(fill_index!=UINT32_MAX && scene->runs[fill_index].kind==BROWSER_SCENE_ROUND) scene->runs[fill_index].offset=(uint32_t)radius;
        if(round_border!=UINT32_MAX) { scene->runs[round_border].height=(uint32_t)box_height; scene->runs[round_border].offset=(uint32_t)radius; }
        if(shadow_index!=UINT32_MAX) {
            browser_scene_run_t *r=&scene->runs[shadow_index];
            int32_t expand=decoration->shadow_blur+decoration->shadow_spread;
            int32_t w=(int32_t)box_width+2*expand,h=box_height+2*expand;
            r->x=box_x+decoration->shadow_x-expand; r->y=box_y+decoration->shadow_y-expand;
            r->width=(uint32_t)(w<0 ? 0 : w); r->height=(uint32_t)(h<0 ? 0 : h);
            int32_t sr=radius+decoration->shadow_spread; r->offset=(uint32_t)(sr<0 ? 0 : sr>64 ? 64 : sr);
        }
        for (unsigned i=0;i<4;++i) if (border_index[i]!=UINT32_MAX) {
            browser_scene_run_t *border=&scene->runs[border_index[i]];
            border->width=(i==0 || i==2) ? box_width : (uint32_t)borders[i];
            border->height=(i==1 || i==3) ? (uint32_t)box_height : (uint32_t)borders[i];
            if (i==1) border->x=box_x+(int32_t)box_width-borders[1];
            if (i==2) border->y=box_y+box_height-borders[2];
        }
        if(inline_box) {
            outer->x=box_x+(int32_t)box_width+margins[1];
            int32_t line=box_height+margins[0]+margins[2]; if(line>outer->line) outer->line=line;
            outer->content=1; outer->pending_space=0;
        } else { outer->y=box_y+box_height; outer->margin=margins[2]; outer->line_start=scene->count; }
        if (outer->y>0 && (uint32_t)outer->y>scene->total_height) scene->total_height=(uint32_t)outer->y;
    }
    return failed ? -28 : 0;
}
static int32_t extra_length(css_computed_style *s,browser_css_length v,int32_t reference) {
    css_unit u=v.unit==BC_EM ? CSS_UNIT_EM : v.unit==BC_REM ? CSS_UNIT_REM :
        v.unit==BC_PERCENT_UNIT ? CSS_UNIT_PCT : v.unit==BC_VW ? CSS_UNIT_VW : v.unit==BC_VH ? CSS_UNIT_VH : CSS_UNIT_PX;
    return length_px(s,v.value,u,reference);
}
static int32_t box_extra(css_computed_style *s,int32_t reference,int vertical) {
    int32_t result=dimension(s,vertical ? css_computed_padding_top : css_computed_padding_left,reference,NULL)+
        dimension(s,vertical ? css_computed_padding_bottom : css_computed_padding_right,reference,NULL);
    dimension_fn fn[]={vertical ? css_computed_border_top_width : css_computed_border_left_width,
                       vertical ? css_computed_border_bottom_width : css_computed_border_right_width};
    uint8_t styles[]={vertical ? css_computed_border_top_style(s) : css_computed_border_left_style(s),
                      vertical ? css_computed_border_bottom_style(s) : css_computed_border_right_style(s)};
    for(unsigned i=0;i<2;++i) if(styles[i]!=CSS_BORDER_STYLE_NONE && styles[i]!=CSS_BORDER_STYLE_HIDDEN) {
        css_fixed value; css_unit unit; uint8_t type=fn[i](s,&value,&unit);
        result+=type==CSS_BORDER_WIDTH_THIN ? 1 : type==CSS_BORDER_WIDTH_MEDIUM ? 2 : type==CSS_BORDER_WIDTH_THICK ? 4 : length_px(s,value,unit,reference);
    }
    if(result<0 || result>BROWSER_SCENE_COORD_LIMIT) { failed=1; return 0; } return result;
}
static int32_t intrinsic_width(node *n,css_computed_style *inherited,int32_t available,uint32_t depth,int minimum) {
    if(depth>=128 || budget()) return 0;
    css_computed_style *s=n->css_style ? n->css_style : inherited;
    if(!s || (n->kind==1 && css_computed_display_static(s)==CSS_DISPLAY_NONE)) return 0;
    if(n->kind==3) {
        css_fixed value; css_unit unit; css_computed_font_size(s,&value,&unit);
        int32_t height=length_px(s,value,unit,16),width=0,max=0;int pending=0;
        uint32_t face=font_face(s),cell=glyph_advance(face,(uint32_t)height,' ',NULL);
        for(size_t i=0;i<n->length;) {
            if(budget()) return 0;
            if(space((uint8_t)n->text[i])) {
                if(minimum) { if(width>max) max=width; width=0; } else pending=width!=0;
                ++i; continue;
            }
            if(pending) { width+=cell; pending=0; }
            width+=(int32_t)glyph_advance(face,(uint32_t)height,font_scalar(n->text+i,n->length-i),NULL); i+=scalar_size(n->text+i,n->length-i);
            if(width>BROWSER_SCENE_COORD_LIMIT) { failed=1; return 0; }
        }
        return width>max ? width : max;
    }
    if(n->kind!=1) return 0;
    int automatic=0; int32_t explicit=dimension(s,css_computed_width,available,&automatic);
    int32_t extra=box_extra(s,available,0);
    if(!automatic) return explicit+(css_computed_box_sizing(s)==CSS_BOX_SIZING_BORDER_BOX ? 0 : extra);
    if(tag(n,"img")) return 160+extra;
    if(n->control_index) {
        const browser_form_control_t *c=&scene->forms.controls[n->control_index-1];
        if(c->kind==BROWSER_FORM_HIDDEN) return 0;
        if(c->kind==BROWSER_FORM_SUBMIT || c->kind==BROWSER_FORM_BUTTON || c->kind==BROWSER_FORM_RESET)
            return (int32_t)control_cells(scene->forms.strings+c->label)*8+16;
        return 168;
    }
    int32_t width=0,line=0; uint32_t count=0;
    uint8_t display=css_computed_display_static(s);
    int flex=display==CSS_DISPLAY_FLEX || display==CSS_DISPLAY_INLINE_FLEX;
    for(node *c=n->first;c;c=c->next) {
        int32_t w=intrinsic_width(c,s,available,depth+1,minimum); ++count;
        uint8_t d=c->css_style ? css_computed_display_static(c->css_style) : CSS_DISPLAY_INLINE;
        if(!flex && d!=CSS_DISPLAY_INLINE && d!=CSS_DISPLAY_INLINE_BLOCK && d!=CSS_DISPLAY_INLINE_FLEX) {
            if(line>width) width=line; if(w>width) width=w; line=0;
        } else { line+=w; if(line>BROWSER_SCENE_COORD_LIMIT) { failed=1; return 0; } }
    }
    if(flex && count>1) line+=(int32_t)(count-1)*extra_length(s,browser_css_values_extra(n->css_values)->column_gap,available);
    if(line>width) width=line;
    return width+extra;
}
typedef struct layout_checkpoint { uint32_t text,links,images,anchors,runs,height; } layout_checkpoint;
static layout_checkpoint checkpoint(void) {
    return (layout_checkpoint){doc->text_length,doc->link_count,doc->image_count,doc->anchor_count,scene->count,scene->total_height};
}
static void restore(layout_checkpoint c) {
    doc->text_length=c.text; doc->link_count=c.links; doc->image_count=c.images; doc->anchor_count=c.anchors;
    scene->count=c.runs; scene->total_height=c.height;
    for(uint32_t i=c.images;i<16;++i) memset(scene->image_urls[i],0,sizeof(scene->image_urls[i]));
}
static int item_layout(node *n,css_computed_style *s,uint32_t link,uint32_t depth,int32_t x,int32_t y,int32_t width,int32_t height,int32_t *used_height) {
    if(width<0 || width>BROWSER_SCENE_COORD_LIMIT || height>BROWSER_SCENE_COORD_LIMIT) return -28;
    flow f={.left=x,.right=x+width,.x=x,.y=y,.line_start=scene->count,.forced_node=n,.forced_width=width,.forced_height=height};
    int rc=layout_node(n,s,&f,link,depth+1,height<0 ? 0 : height);
    end_line(&f,0); *used_height=f.y-y;
    return rc || failed ? -28 : 0;
}
typedef struct layout_item {
    node *n;
    int32_t cross,cross_before,cross_after;
    uint32_t align;
    int cross_auto;
} layout_item;
typedef struct container_plan {
    layout_item items[BROWSER_CSS_ITEMS];
    browser_css_flex_item flex[BROWSER_CSS_ITEMS];
    uint32_t start[BROWSER_CSS_ITEMS],end[BROWSER_CSS_ITEMS],lines,count;
    int32_t cross[BROWSER_CSS_ITEMS],cross_position[BROWSER_CSS_ITEMS];
} container_plan;
static int container_run(node *n,css_computed_style *s,flow *f,uint32_t link,uint32_t depth,int32_t definite,container_plan *p) {
    const browser_css_extra *ext=browser_css_values_extra(n->css_values);
    int grid=ext->grid!=0;
    uint8_t direction=css_computed_flex_direction(s);
    int column=!grid && (direction==CSS_FLEX_DIRECTION_COLUMN || direction==CSS_FLEX_DIRECTION_COLUMN_REVERSE);
    int reverse=!grid && (direction==CSS_FLEX_DIRECTION_ROW_REVERSE || direction==CSS_FLEX_DIRECTION_COLUMN_REVERSE);
    int32_t available=f->right-f->left,main=column ? definite : available;
    int32_t gap=extra_length(s,column ? ext->row_gap : ext->column_gap,available);
    int32_t cross_gap=extra_length(s,column ? ext->column_gap : ext->row_gap,available);
    if(gap<0 || cross_gap<0) return -28;
    for(node *c=n->first;c;c=c->next) {
        if(budget()) return -28;
        if(c->kind==3) { size_t i=0; while(i<c->length && space((uint8_t)c->text[i])) { if(budget()) return -28; ++i; } if(i==c->length) continue; }
        else if(c->kind!=1 || !c->css_style || css_computed_display_static(c->css_style)==CSS_DISPLAY_NONE) continue;
        if(p->count==BROWSER_CSS_ITEMS) return -28;
        uint32_t i=p->count++; layout_item *item=&p->items[i]; browser_css_flex_item *v=&p->flex[i]; item->n=c;
        css_computed_style *cs=c->css_style ? c->css_style : s;
        int anonymous=c->kind==3;
        int auto_width=0;
        int32_t width=dimension(cs,css_computed_width,available,&auto_width),extra=box_extra(cs,available,0);
        if(anonymous) { auto_width=1; extra=0; }
        if(auto_width) width=intrinsic_width(c,s,available,depth+1,0);
        else if(css_computed_box_sizing(cs)==CSS_BOX_SIZING_CONTENT_BOX) width+=extra;
        if(width>available && auto_width) width=available;
        if(width<extra) width=extra;
        item->align=c->kind==3 ? CSS_ALIGN_ITEMS_STRETCH : css_computed_align_self(cs);
        if(item->align==CSS_ALIGN_SELF_AUTO) item->align=css_computed_align_items(s);
        item->cross_auto=anonymous || (column ? auto_width : css_computed_height(cs,&(css_fixed){0},&(css_unit){0})==CSS_HEIGHT_AUTO);
        if(column && auto_width && item->align==CSS_ALIGN_ITEMS_STRETCH) width=available;
        if(column) {
            layout_checkpoint saved=checkpoint(); int32_t measured=0;
            int rc=item_layout(c,s,link,depth,0,0,width,-1,&measured); restore(saved); if(rc) return rc;
            item->cross=width; v->basis=measured;
        } else v->basis=width;
        css_fixed fixed=0; css_unit unit=CSS_UNIT_PX;
        if(!anonymous && !grid && css_computed_flex_basis(cs,&fixed,&unit)==CSS_FLEX_BASIS_SET && (!column || definite || unit!=CSS_UNIT_PCT))
            v->basis=length_px(cs,fixed,unit,main)+box_extra(cs,available,column);
        v->before=dimension(cs,column ? css_computed_margin_top : css_computed_margin_left,available,NULL);
        v->after=dimension(cs,column ? css_computed_margin_bottom : css_computed_margin_right,available,NULL);
        item->cross_before=dimension(cs,column ? css_computed_margin_left : css_computed_margin_top,available,NULL);
        item->cross_after=dimension(cs,column ? css_computed_margin_right : css_computed_margin_bottom,available,NULL);
        int min_auto=0,max_auto=0; int32_t axis_extra=box_extra(cs,available,column);
        v->minimum=dimension(cs,column ? css_computed_min_height : css_computed_min_width,main,&min_auto);
        if(min_auto && !column) v->minimum=intrinsic_width(c,s,available,depth+1,1);
        else if(css_computed_box_sizing(cs)==CSS_BOX_SIZING_CONTENT_BOX) v->minimum+=axis_extra;
        v->maximum=dimension(cs,column ? css_computed_max_height : css_computed_max_width,main,&max_auto);
        if(max_auto) v->maximum=BROWSER_SCENE_COORD_LIMIT;
        else if(css_computed_box_sizing(cs)==CSS_BOX_SIZING_CONTENT_BOX) v->maximum+=axis_extra;
        if(v->maximum<v->minimum) v->maximum=v->minimum;
        css_computed_flex_grow(cs,&fixed); v->grow=fixed;
        css_computed_flex_shrink(cs,&fixed); v->shrink=fixed;
        if(anonymous) {
            v->before=v->after=item->cross_before=item->cross_after=0;
            v->minimum=column ? 0 : intrinsic_width(c,s,available,depth+1,1);
            v->maximum=BROWSER_SCENE_COORD_LIMIT; v->grow=0; v->shrink=1024;
        }
    }
    if(!p->count) return 0;
    int32_t columns[16]={0}; uint32_t tracks=ext->tracks;
    if(grid) {
        if(!tracks) { tracks=1; columns[0]=available; }
        else {
            if(ext->auto_fit) {
                int32_t minimum=extra_length(s,ext->columns[0].minimum,available);
                if(minimum<=0 || minimum> BROWSER_SCENE_COORD_LIMIT-gap) return -28;
                tracks=(uint32_t)((available+gap)/(minimum+gap)); if(!tracks) tracks=1;
                if(tracks>p->count) tracks=p->count; if(tracks>16) return -28;
            }
            int32_t minima[16],fractions[16];
            for(uint32_t i=0;i<tracks;++i) {
                const browser_css_track *t=&ext->columns[ext->auto_fit ? 0 : i];
                minima[i]=extra_length(s,t->minimum,available);
                fractions[i]=t->maximum.unit==BC_FR ? t->maximum.value : 0;
                if(!fractions[i]) { int32_t maximum=extra_length(s,t->maximum,available); if(maximum>minima[i]) minima[i]=maximum; }
            }
            if(browser_css_grid_columns(columns,minima,fractions,tracks,available,gap)) return -28;
        }
    }
    if(column && !main) {
        int64_t total=(int64_t)(p->count-1)*gap;
        for(uint32_t i=0;i<p->count;++i) total+=(int64_t)p->flex[i].basis+p->flex[i].before+p->flex[i].after;
        if(total<0 || total>BROWSER_SCENE_COORD_LIMIT) return -28; main=(int32_t)total;
    }
    uint8_t wrap=css_computed_flex_wrap(s); uint32_t start=0;
    while(start<p->count) {
        uint32_t end=start; int64_t used=0;
        while(end<p->count) {
            browser_css_flex_item *v=&p->flex[end];
            int32_t hypothetical=v->basis<v->minimum ? v->minimum : v->basis>v->maximum ? v->maximum : v->basis;
            int64_t next=(int64_t)hypothetical+v->before+v->after+(end>start ? gap : 0);
            if(end>start && (grid ? end-start==tracks : wrap!=CSS_FLEX_WRAP_NOWRAP && used+next>main)) break;
            used+=next; ++end;
        }
        uint32_t line=p->lines++; p->start[line]=start; p->end[line]=end;
        if(grid) {
            int32_t x=0;
            for(uint32_t i=start;i<end;++i) { p->flex[i].position=x+p->flex[i].before; p->flex[i].size=columns[i-start]-p->flex[i].before-p->flex[i].after; x+=columns[i-start]+gap; }
        } else {
            uint8_t j=css_computed_justify_content(s);
            uint32_t justify=j==CSS_JUSTIFY_CONTENT_FLEX_END ? 1 : j==CSS_JUSTIFY_CONTENT_CENTER ? 2 :
                j==CSS_JUSTIFY_CONTENT_SPACE_BETWEEN ? 3 : j==CSS_JUSTIFY_CONTENT_SPACE_AROUND ? 4 : j==CSS_JUSTIFY_CONTENT_SPACE_EVENLY ? 5 : 0;
            if(browser_css_flex_line(p->flex+start,end-start,main,gap,justify,reverse)) return -28;
        }
        for(uint32_t i=start;i<end;++i) {
            layout_item *item=&p->items[i]; browser_css_flex_item *v=&p->flex[i];
            if(!column) {
                layout_checkpoint saved=checkpoint(); int32_t measured=0;
                int rc=item_layout(item->n,s,link,depth,0,0,v->size,-1,&measured); restore(saved); if(rc) return rc;
                item->cross=measured;
            }
            int32_t cross=item->cross+item->cross_before+item->cross_after;
            if(cross>p->cross[line]) p->cross[line]=cross;
        }
        start=end;
    }
    int32_t total_cross=(int32_t)(p->lines-1)*cross_gap;
    for(uint32_t l=0;l<p->lines;++l) total_cross+=p->cross[l];
    int32_t cross_available=column ? available : definite;
    int32_t free=cross_available>total_cross ? cross_available-total_cross : 0;
    uint8_t align=css_computed_align_content(s);
    int32_t cross_at=align==CSS_ALIGN_CONTENT_FLEX_END ? free : align==CSS_ALIGN_CONTENT_CENTER ? free/2 : 0;
    if(!grid && wrap==CSS_FLEX_WRAP_NOWRAP && p->lines==1 && free) { p->cross[0]+=free; cross_at=0; free=0; }
    for(uint32_t l=0;l<p->lines;++l) {
        if(align==CSS_ALIGN_CONTENT_STRETCH) p->cross[l]+=free/(int32_t)p->lines;
        int32_t spacing=align==CSS_ALIGN_CONTENT_SPACE_BETWEEN && p->lines>1 ? free*(int32_t)l/(int32_t)(p->lines-1) :
            align==CSS_ALIGN_CONTENT_SPACE_AROUND ? free*(int32_t)(2*l+1)/(int32_t)(2*p->lines) :
            align==CSS_ALIGN_CONTENT_SPACE_EVENLY ? free*(int32_t)(l+1)/(int32_t)(p->lines+1) : 0;
        p->cross_position[l]=cross_at+spacing; cross_at+=p->cross[l]+cross_gap;
    }
    int32_t cross_extent=cross_available>total_cross ? cross_available : total_cross;
    for(uint32_t l=0;l<p->lines;++l) {
        int32_t line_at=p->cross_position[l];
        if(!grid && wrap==CSS_FLEX_WRAP_WRAP_REVERSE) line_at=cross_extent-line_at-p->cross[l];
        for(uint32_t i=p->start[l];i<p->end[l];++i) {
            layout_item *item=&p->items[i]; browser_css_flex_item *v=&p->flex[i];
            int32_t free_cross=p->cross[l]-item->cross-item->cross_before-item->cross_after;
            int32_t shift=item->align==CSS_ALIGN_ITEMS_FLEX_END ? free_cross : item->align==CSS_ALIGN_ITEMS_CENTER ? free_cross/2 : 0;
            int32_t cross_size=item->cross;
            if(item->cross_auto && (grid || item->align==CSS_ALIGN_ITEMS_STRETCH)) cross_size+=free_cross;
            int32_t x=f->left+(column ? line_at+item->cross_before+shift : v->position);
            int32_t y=f->y+(column ? v->position : line_at+item->cross_before+shift),height=0;
            if(item_layout(item->n,s,link,depth,x,y,column ? cross_size : v->size,column ? v->size : cross_size,&height)) return -28;
        }
    }
    f->y+=column ? main : cross_extent; f->x=f->left; f->line_start=scene->count; f->margin=0;
    return failed ? -28 : 0;
}
static int layout_container(node *n,css_computed_style *s,flow *f,uint32_t link,uint32_t depth,int32_t definite) {
    container_plan *p=calloc(1,sizeof(*p)); if(!p) return -28;
    int rc=container_run(n,s,f,link,depth,definite,p); free(p); return rc;
}
static void title_text(node *n) {
    if (tag(n,"title")) {
        size_t used=0;
        for (node *c=n->first;c;c=c->next) if (c->kind==3 && c->length<128-used) {
            memcpy(doc->title+used,c->text,c->length); used+=c->length;
        }
        doc->title[used]=0; return;
    }
    for (node *c=n->first;c;c=c->next) title_text(c);
}
static int render_document(const uint8_t *html,size_t length,uint32_t width,uint32_t height,
    const uint32_t image_sizes[16][2],const char *url,const browser_resources_t *bundle,
    browser_resource_needs_t *needs,reist_html_document_t *document,browser_scene_t *output,
    uint32_t extended,uint32_t encoding) {
    if (!document || !output || width<1 || width>1024 || !height || height>768) return -22;
    if(bundle && (!needs || browser_resources_validate(bundle,url,bundle->generation))) return -84;
    resources=bundle; resource_needs=needs; imported_count=0; document_profile=extended;
    if(needs) memset(needs,0,offsetof(browser_resource_needs_t,items));
    node *root; int result=extended ? browser_html5_document_tree(html,length,encoding,&root) :
        browser_html5_tree_with_heap(html,length,&root,bundle && bundle->count); if (result) return result;
    work=failed=sheet_count=0; context=NULL; doc=document; scene=output; intrinsic=image_sizes;
    browser_css_values_reset(css_value_budget);
    document_url=url ? url : "/document.html";
    memset(doc,0,sizeof(*doc)); memset(scene,0,sizeof(*scene));
    if(browser_font_begin(&scene->fonts,css_value_budget)) { browser_css_values_release();if(extended)browser_html5_document_release();return -28; }
    scene->version=extended ? BROWSER_SCENE_DOCUMENT_VERSION : BROWSER_SCENE_VERSION; scene->width=width; scene->height=height;
    if(browser_forms_project(root,&scene->forms)) { browser_font_finish();browser_css_values_release(); if(extended) browser_html5_document_release(); return -28; }
    units.viewport_width=INTTOFIX(width); units.viewport_height=INTTOFIX(height);
    units.font_size_default=INTTOFIX(16); units.font_size_minimum=INTTOFIX(1);
    units.device_dpi=INTTOFIX(96); units.root_style=NULL;
    media=(css_media){.type=CSS_MEDIA_SCREEN,.width=INTTOFIX(width),.height=INTTOFIX(height)};
    static const char ua[]="html,body,div,p,form,fieldset,section,article,header,footer,main,nav,ul,ol,li,pre,table,tr,h1,h2,h3,h4,h5,h6 {display:block}"
        "head,script,style,template {display:none} body {margin:4px 16px} p,div,pre,ul,ol {margin-top:7px;margin-bottom:7px}"
        "h1 {font-size:24px;margin:7px 0;color:#203070} h2 {font-size:20px;margin:7px 0} h3 {font-size:18px;margin:7px 0}"
        "b,strong,h1,h2,h3,h4,h5,h6 {font-weight:bold} i,em {font-style:italic} a:link {color:#0000cc;text-decoration:underline} pre {white-space:pre} img {display:block}";
    if (css_select_ctx_create(&context)!=CSS_OK) result=-28;
    if (!result) result=add_sheet(ua,sizeof(ua)-1,CSS_ORIGIN_UA,NULL);
    if (!result && browser_html_script_enabled()) result=add_sheet("noscript{display:none}",22,CSS_ORIGIN_UA,NULL);
    if (!result) result=collect_sheets(root,0);
    if (!result && needs && needs->count) result=1;
    if (!result) result=select_tree(root,NULL,0);
    if (!result) {
        title_text(root);
        flow f={.right=(int32_t)width};
        result=layout_node(root,NULL,&f,UINT32_MAX,0,(int32_t)height);
        if(scene->fonts.count)scene->version=BROWSER_SCENE_FONT_VERSION;
        if (!result) result=browser_scene_validate(doc,scene);
    }
    cleanup_tree(root);
    if (context) css_select_ctx_destroy(context);
    while (sheet_count) css_stylesheet_destroy(sheets[--sheet_count]);
    while (imported_count) css_stylesheet_destroy(imported[--imported_count]);
    browser_css_values_release();
    browser_font_finish();
    if(extended) browser_html5_document_release();
    return result;
}
int browser_css_render_document(const uint8_t *html,size_t length,uint32_t width,uint32_t height,
    const uint32_t image_sizes[16][2],const char *url,const browser_resources_t *bundle,
    browser_resource_needs_t *needs,reist_html_document_t *document,browser_scene_t *output,uint32_t encoding) {
    return render_document(html,length,width,height,image_sizes,url,bundle,needs,document,output,1,encoding);
}
int browser_css_render_resources(const uint8_t *html,size_t length,uint32_t width,uint32_t height,
    const uint32_t image_sizes[16][2],const char *url,const browser_resources_t *bundle,
    browser_resource_needs_t *needs,reist_html_document_t *document,browser_scene_t *output) {
    return render_document(html,length,width,height,image_sizes,url,bundle,needs,document,output,0,0);
}
int browser_css_render(const uint8_t *html,size_t length,uint32_t width,uint32_t height,
    const uint32_t image_sizes[16][2],const char *url,reist_html_document_t *document,browser_scene_t *output) {
    return browser_css_render_resources(html,length,width,height,image_sizes,url,NULL,NULL,document,output);
}
