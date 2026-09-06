#include "browser_forms.h"
#include "html_engine.h"
#include "reist/gui/html_document.h"
#include <string.h>

static int editable(uint32_t kind) { return kind==BROWSER_FORM_TEXT || kind==BROWSER_FORM_TEXTAREA; }
static int utf8(const char *s,size_t n) {
    for(size_t i=0;i<n;) {
        uint32_t c=(uint8_t)s[i++], more=0, minimum=0;
        if(!c) return 0;
        if(c<128) continue;
        if(c>=0xc2 && c<=0xdf) { c&=31; more=1; minimum=128; }
        else if(c>=0xe0 && c<=0xef) { c&=15; more=2; minimum=2048; }
        else if(c>=0xf0 && c<=0xf4) { c&=7; more=3; minimum=65536; }
        else return 0;
        if(more>n-i) return 0;
        while(more--) { uint32_t b=(uint8_t)s[i++]; if((b&192)!=128) return 0; c=(c<<6)|(b&63); }
        if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0;
    }
    return 1;
}
static int string_valid(const browser_forms_t *m,uint32_t o) {
    /* Canonical offsets point to the beginning, not inside another string. */
    return o<m->used && (!o || !m->strings[o-1]);
}
int browser_forms_validate(const browser_forms_t *m) {
    if(m && !m->version && !m->form_count && !m->control_count && !m->option_count && !m->used) return 0;
    if(!m || m->version!=BROWSER_FORMS_VERSION || m->form_count>BROWSER_FORM_COUNT ||
       m->control_count>BROWSER_FORM_CONTROLS || m->option_count>BROWSER_FORM_OPTIONS ||
       !m->used || m->used>BROWSER_FORM_BYTES || m->strings[0] || m->strings[m->used-1]) return -84;
    for(uint32_t o=0;o<m->used;) {
        uint32_t end=o; while(end<m->used && m->strings[end]) ++end;
        if(end==m->used || !utf8(m->strings+o,end-o)) return -84;
        o=end+1;
    }
    for(uint32_t i=0;i<m->form_count;++i)
        if(!string_valid(m,m->forms[i].action) || m->forms[i].blocked>1) return -84;
    uint32_t options=0;
    for(uint32_t i=0;i<m->control_count;++i) {
        const browser_form_control_t *c=&m->controls[i];
        if(c->kind<BROWSER_FORM_TEXT || c->kind>BROWSER_FORM_UNSUPPORTED ||
           (c->owner!=BROWSER_FORM_NONE && c->owner>=m->form_count) || (c->flags&~31U) ||
           !string_valid(m,c->name) || !string_valid(m,c->value) || !string_valid(m,c->label) ||
           c->first_option!=options || c->option_count>m->option_count-options) return -84;
        if((c->flags&BROWSER_FORM_READONLY) && !editable(c->kind)) return -84;
        if((c->flags&BROWSER_FORM_CHECKED) && c->kind!=BROWSER_FORM_CHECKBOX && c->kind!=BROWSER_FORM_RADIO) return -84;
        if((c->flags&BROWSER_FORM_MULTIPLE || c->option_count) && c->kind!=BROWSER_FORM_SELECT) return -84;
        if(c->target!=BROWSER_FORM_NONE && (c->kind!=BROWSER_FORM_LABEL || c->target>=m->control_count ||
            m->controls[c->target].kind==BROWSER_FORM_LABEL || m->controls[c->target].kind==BROWSER_FORM_HIDDEN)) return -84;
        uint32_t selected=0;
        for(uint32_t j=0;j<c->option_count;++j) {
            const browser_form_option_t *o=&m->options[options+j];
            if(!string_valid(m,o->value) || !string_valid(m,o->label) || (o->flags&~5U)) return -84;
            selected+=!!(o->flags&BROWSER_FORM_CHECKED);
        }
        if(c->kind==BROWSER_FORM_SELECT && !(c->flags&BROWSER_FORM_MULTIPLE) && selected>1) return -84;
        options+=c->option_count;
    }
    return options==m->option_count ? 0 : -84;
}
static int state_valid(const browser_forms_t *m,const browser_form_state_t *s) {
    if(!s || !s->generation || s->used>BROWSER_FORM_BYTES ||
       (s->focus!=BROWSER_FORM_NONE && s->focus>=m->control_count) ||
       (s->capture!=BROWSER_FORM_NONE && s->capture>=m->control_count)) return 0;
    uint32_t used=0;
    for(uint32_t i=0;i<m->control_count;++i) {
        if(s->checked[i]>1 || (s->checked[i] && m->controls[i].kind!=BROWSER_FORM_CHECKBOX && m->controls[i].kind!=BROWSER_FORM_RADIO)) return 0;
        if(editable(m->controls[i].kind)) {
            if(used>s->used || s->offsets[i]!=used || s->lengths[i]>=s->used-used ||
               s->values[used+s->lengths[i]] || !utf8(s->values+used,s->lengths[i])) return 0;
            used+=s->lengths[i]+1;
        }
    }
    if(used!=s->used) return 0;
    for(uint32_t i=0;i<m->option_count;++i) if(s->selected[i]>1) return 0;
    for(uint32_t i=0;i<m->control_count;++i) {
        const browser_form_control_t *c=&m->controls[i]; uint32_t count=0;
        for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j) count+=s->selected[j];
        if(!(c->flags&BROWSER_FORM_MULTIPLE) && count>1) return 0;
    }
    return 1;
}
const char *browser_forms_value(const browser_forms_t *m,const browser_form_state_t *s,uint32_t i) {
    if(i>=m->control_count) return "";
    return editable(m->controls[i].kind) ? s->values+s->offsets[i] : m->strings+m->controls[i].value;
}
static void radio(const browser_forms_t *m,browser_form_state_t *s,uint32_t i) {
    const browser_form_control_t *c=&m->controls[i]; const char *name=m->strings+c->name;
    if(*name) for(uint32_t j=0;j<m->control_count;++j)
        if(m->controls[j].kind==BROWSER_FORM_RADIO && m->controls[j].owner==c->owner &&
           !strcmp(name,m->strings+m->controls[j].name)) s->checked[j]=0;
    s->checked[i]=1;
}
int browser_forms_bind(const browser_forms_t *m,const browser_forms_t *old,browser_form_state_t *s,uint32_t generation,int reflow) {
    if(!generation || browser_forms_validate(m)) return -84;
    if(reflow) {
        if(!old || s->generation!=generation || memcmp(m,old,sizeof(*m)) || !state_valid(m,s)) return -84;
        s->capture=BROWSER_FORM_NONE; return 0;
    }
    uint32_t need=0;
    for(uint32_t i=0;i<m->control_count;++i) if(editable(m->controls[i].kind)) {
        size_t n=strlen(m->strings+m->controls[i].value)+1;
        if(n>BROWSER_FORM_BYTES-need) return -28;
        need+=(uint32_t)n;
    }
    memset(s,0,sizeof(*s)); s->generation=generation;
    s->focus=s->capture=BROWSER_FORM_NONE;
    for(uint32_t i=0;i<m->control_count;++i) {
        const browser_form_control_t *c=&m->controls[i];
        if(editable(c->kind)) {
            const char *v=m->strings+c->value; uint32_t n=(uint32_t)strlen(v);
            s->offsets[i]=s->used; s->lengths[i]=n; memcpy(s->values+s->used,v,n+1); s->used+=n+1;
        }
        if(c->flags&BROWSER_FORM_CHECKED) {
            if(c->kind==BROWSER_FORM_RADIO) radio(m,s,i); else s->checked[i]=1;
        }
    }
    for(uint32_t i=0;i<m->option_count;++i) s->selected[i]=!!(m->options[i].flags&BROWSER_FORM_CHECKED);
    return 0;
}
static void replace(const browser_forms_t *m,browser_form_state_t *s,uint32_t i,uint32_t at,uint32_t remove,const char *bytes,uint32_t n) {
    uint32_t pos=s->offsets[i]+at, end=pos+remove;
    memmove(s->values+pos+n,s->values+end,s->used-end); memcpy(s->values+pos,bytes,n);
    s->used=s->used-remove+n; s->lengths[i]=s->lengths[i]-remove+n;
    for(uint32_t j=i+1;j<m->control_count;++j) if(editable(m->controls[j].kind)) s->offsets[j]=s->offsets[j]-remove+n;
}
int browser_forms_reset(const browser_forms_t *m,browser_form_state_t *s,uint32_t owner) {
    if(owner>=m->form_count || !state_valid(m,s)) return -84;
    uint32_t final=s->used;
    for(uint32_t i=0;i<m->control_count;++i) if(m->controls[i].owner==owner && editable(m->controls[i].kind))
        final=final-s->lengths[i]+(uint32_t)strlen(m->strings+m->controls[i].value);
    if(final>BROWSER_FORM_BYTES) return -28;
    /* Shrink first: a later shrink must not be needed to admit an early grow. */
    for(unsigned pass=0;pass<2;++pass) for(uint32_t i=0;i<m->control_count;++i) {
        const browser_form_control_t *c=&m->controls[i]; if(c->owner!=owner) continue;
        if(editable(c->kind)) {
            const char *v=m->strings+c->value; uint32_t n=(uint32_t)strlen(v);
            if((n<=s->lengths[i])==!pass) replace(m,s,i,0,s->lengths[i],v,n);
        }
        if(!pass) {
            s->checked[i]=!!(c->flags&BROWSER_FORM_CHECKED);
            for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j)
                s->selected[j]=!!(m->options[j].flags&BROWSER_FORM_CHECKED);
        }
    }
    for(uint32_t i=0;i<m->control_count;++i) if(m->controls[i].owner==owner &&
        m->controls[i].kind==BROWSER_FORM_RADIO && (m->controls[i].flags&BROWSER_FORM_CHECKED)) radio(m,s,i);
    s->cursor=0; s->capture=BROWSER_FORM_NONE; return 0;
}
int browser_forms_focus(const browser_forms_t *m,browser_form_state_t *s,uint32_t i) {
    if(i>=m->control_count || (m->controls[i].flags&BROWSER_FORM_DISABLED) ||
       m->controls[i].kind==BROWSER_FORM_HIDDEN || m->controls[i].kind==BROWSER_FORM_LABEL) return -22;
    s->focus=i; s->cursor=editable(m->controls[i].kind) ? s->lengths[i] : 0; return 0;
}
int browser_forms_activate(const browser_forms_t *m,browser_form_state_t *s,uint32_t i) {
    if(i>=m->control_count) return -22;
    if(m->controls[i].kind==BROWSER_FORM_LABEL) i=m->controls[i].target;
    if(browser_forms_focus(m,s,i)) return 0;
    const browser_form_control_t *c=&m->controls[i];
    if(c->kind==BROWSER_FORM_CHECKBOX) s->checked[i]^=1;
    else if(c->kind==BROWSER_FORM_RADIO) radio(m,s,i);
    else if(c->kind==BROWSER_FORM_RESET) return browser_forms_reset(m,s,c->owner) ? -28 : 1;
    return 1;
}
int browser_forms_key(const browser_forms_t *m,browser_form_state_t *s,uint32_t key) {
    uint32_t i=s->focus; if(i>=m->control_count) return 0;
    const browser_form_control_t *c=&m->controls[i];
    if(c->flags&BROWSER_FORM_DISABLED) return 0;
    if(c->kind==BROWSER_FORM_SELECT && (key==258 || key==259 || key==' ')) {
        uint32_t start=c->first_option,end=start+c->option_count,current=end;
        for(uint32_t j=start;j<end;++j) if(s->selected[j]) { current=j; break; }
        for(uint32_t k=0;k<c->option_count;++k) {
            current=key==258 ? (current<=start ? end-1 : current-1) : (current+1>=end ? start : current+1);
            if(!(m->options[current].flags&BROWSER_FORM_DISABLED)) {
                if(c->flags&BROWSER_FORM_MULTIPLE) s->selected[current]^=1;
                else { for(uint32_t j=start;j<end;++j) s->selected[j]=0; s->selected[current]=1; }
                break;
            }
        }
        return 1;
    }
    if(!editable(c->kind)) return key==' ' ? browser_forms_activate(m,s,i) : 0;
    const char *v=s->values+s->offsets[i]; uint32_t length=s->lengths[i],cursor=s->cursor;
    if(cursor>length || (cursor<length && ((uint8_t)v[cursor]&192)==128)) return -84;
    if(key==260) { if(cursor) do { --cursor; } while(cursor && ((uint8_t)v[cursor]&192)==128); s->cursor=cursor; return 1; }
    if(key==261) { if(cursor<length) do { ++cursor; } while(cursor<length && ((uint8_t)v[cursor]&192)==128); s->cursor=cursor; return 1; }
    if(key==262 || key==263) { s->cursor=key==262 ? 0 : length; return 1; }
    if(c->flags&BROWSER_FORM_READONLY) return 1;
    uint32_t remove=0,n=0; char bytes[4];
    if(key==8) {
        if(!cursor) return 1;
        uint32_t old=cursor; do { --cursor; } while(cursor && ((uint8_t)v[cursor]&192)==128); remove=old-cursor;
    } else if(key==127 || key==264) {
        if(cursor==length) return 1;
        remove=1; while(cursor+remove<length && ((uint8_t)v[cursor+remove]&192)==128) ++remove;
    } else if((key=='\n' || key=='\r') && c->kind==BROWSER_FORM_TEXTAREA) { bytes[0]='\n'; n=1; }
    else if(key>=32 && key<127) { bytes[0]=(char)key; n=1; }
    /* 257..266 are the existing shell/Surface key namespace, not text. */
    else if(key>=128 && key<=0x10ffff && !(key>=257 && key<=266) && !(key>=0xd800 && key<=0xdfff)) {
        if(key<2048) { bytes[0]=(char)(192|(key>>6)); bytes[1]=(char)(128|(key&63)); n=2; }
        else if(key<65536) { bytes[0]=(char)(224|(key>>12)); bytes[1]=(char)(128|((key>>6)&63)); bytes[2]=(char)(128|(key&63)); n=3; }
        else { bytes[0]=(char)(240|(key>>18)); bytes[1]=(char)(128|((key>>12)&63)); bytes[2]=(char)(128|((key>>6)&63)); bytes[3]=(char)(128|(key&63)); n=4; }
    } else return 0;
    if(n>BROWSER_FORM_BYTES-(s->used-remove)) return -28;
    replace(m,s,i,cursor,remove,bytes,n); s->cursor=cursor+n; return 1;
}
static int put(char *out,size_t cap,size_t *used,char c) {
    if(*used+1>=cap) return -28; out[(*used)++]=c; return 0;
}
static int encode_byte(char *out,size_t cap,size_t *used,uint8_t c) {
    static const char hex[]="0123456789ABCDEF";
    if(c==' ') return put(out,cap,used,'+');
    if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='*' || c=='-' || c=='.' || c=='_') return put(out,cap,used,(char)c);
    return put(out,cap,used,'%') || put(out,cap,used,hex[c>>4]) || put(out,cap,used,hex[c&15]) ? -28 : 0;
}
static int encoded(char *out,size_t cap,size_t *used,const char *v) {
    for(size_t i=0;v[i];++i) {
        uint8_t c=(uint8_t)v[i];
        if(c=='\r' || c=='\n') {
            if(c=='\r' && v[i+1]=='\n') ++i;
            if(encode_byte(out,cap,used,'\r') || encode_byte(out,cap,used,'\n')) return -28;
        } else if(encode_byte(out,cap,used,c)) return -28;
    }
    return 0;
}
static int pair(char *out,size_t cap,size_t *used,uint32_t *count,const char *name,const char *value) {
    if((*count)++ && put(out,cap,used,'&')) return -28;
    return encoded(out,cap,used,name) || put(out,cap,used,'=') || encoded(out,cap,used,value) ? -28 : 0;
}
int browser_forms_submit(const browser_forms_t *m,const browser_form_state_t *s,uint32_t generation,
                         uint32_t submitter,const char *base,char *url,size_t capacity) {
    if(!url || !base || !capacity || browser_forms_validate(m) || !state_valid(m,s) ||
       generation!=s->generation || submitter>=m->control_count) return -84;
    const browser_form_control_t *button=&m->controls[submitter]; uint32_t owner=button->owner;
    if(button->kind!=BROWSER_FORM_SUBMIT || (button->flags&BROWSER_FORM_DISABLED) || owner>=m->form_count) return -22;
    if(m->forms[owner].blocked) return -95;
    for(uint32_t i=0;i<m->control_count;++i) if(m->controls[i].owner==owner &&
       !(m->controls[i].flags&BROWSER_FORM_DISABLED) && (m->controls[i].flags&BROWSER_FORM_BLOCKED)) return -95;
    char action[256],candidate[256]; const char *a=m->strings+m->forms[owner].action;
    if(reist_html_url_resolve(base,*a ? a : base,action,sizeof(action))) return -22;
    const char *authority=!strncmp(action,"https://",8) ? action+8 : !strncmp(action,"http://",7) ? action+7 : NULL;
    if(!authority || !*authority) return -95;
    for(const char *p=authority;*p && *p!='/' && *p!='?' && *p!='#';++p) if(*p=='@') return -95;
    size_t cap=capacity<sizeof(candidate) ? capacity : sizeof(candidate),used=0; const char *fragment=strchr(action,'#');
    for(const char *p=action;*p && *p!='?' && *p!='#';++p) if(put(candidate,cap,&used,*p)) return -28;
    if(put(candidate,cap,&used,'?')) return -28;
    uint32_t count=0;
    for(uint32_t i=0;i<m->control_count;++i) {
        const browser_form_control_t *c=&m->controls[i]; const char *name=m->strings+c->name;
        if(c->owner!=owner || !*name || (c->flags&BROWSER_FORM_DISABLED) || c->kind>=BROWSER_FORM_RESET ||
           (c->kind==BROWSER_FORM_SUBMIT && i!=submitter) ||
           ((c->kind==BROWSER_FORM_CHECKBOX || c->kind==BROWSER_FORM_RADIO) && !s->checked[i])) continue;
        if(c->kind==BROWSER_FORM_SELECT) {
            for(uint32_t j=c->first_option;j<c->first_option+c->option_count;++j)
                if(s->selected[j] && !(m->options[j].flags&BROWSER_FORM_DISABLED) &&
                   pair(candidate,cap,&used,&count,name,m->strings+m->options[j].value)) return -28;
        } else {
            const char *v=c->kind==BROWSER_FORM_HIDDEN && !strcmp(name,"_charset_") ? "UTF-8" : browser_forms_value(m,s,i);
            if(pair(candidate,cap,&used,&count,name,v)) return -28;
        }
    }
    if(fragment) for(const char *p=fragment;*p;++p) if(put(candidate,cap,&used,*p)) return -28;
    candidate[used]=0; memcpy(url,candidate,used+1); return 0;
}
const char *browser_forms_error(int rc) {
    return rc==-28 ? "Formulargrenze: Eingabe oder URL zu lang" : rc==-95 ?
        "Formular nicht unterstuetzt - nichts gesendet" : "Formular ungueltig - nichts gesendet";
}

/* The retained Hubbub tree is bounded to 2048 nodes/depth 128. These passes
 * visit it in tree order; no tokenization or second HTML parser. */
static const char *attribute_value(const node *n,const char *name) {
    for(const attribute *a=n->attributes;a;a=a->next) if(!strcmp(a->name,name)) return a->value;
    return NULL;
}
static int element(const node *n,const char *name) { return n->kind==1 && n->ns==1 && !strcmp(n->name,name); }
static int equal_ascii(const char *a,const char *b) {
    if(!a) return 0;
    while(*a && *b) { unsigned x=(uint8_t)*a++,y=(uint8_t)*b++; if(x>='A' && x<='Z') x+=32; if(x!=y) return 0; }
    return !*a && !*b;
}
static int add_string(browser_forms_t *m,const char *v,uint32_t *out,int lines) {
    if(!v || !*v) { *out=0; return 0; }
    uint32_t start=m->used;
    for(size_t i=0;v[i];++i) {
        char c=v[i];
        if(lines==1 && (c=='\n' || c=='\r')) continue;
        if(lines==2 && c=='\r') { c='\n'; if(v[i+1]=='\n') ++i; }
        if(m->used+1>=BROWSER_FORM_BYTES) return -28;
        m->strings[m->used++]=c;
    }
    m->strings[m->used++]=0; *out=start; return 0;
}
static int text_content(node *n,browser_forms_t *m,uint32_t *out,int collapse) {
    uint32_t start=m->used; *out=start;
    /* Iterative bounded subtree walk, excluding nested select/option scripts. */
    node *c=n->first; uint32_t seen=0; int space=0;
    while(c) {
        if(++seen>4096) return -28;
        if(c->kind==3) for(size_t j=0;j<c->length;++j) {
            char b=c->text[j]; if(!b) return -84;
            if(collapse && (b==' ' || b=='\n' || b=='\r' || b=='\t' || b=='\f')) { space=m->used>start; continue; }
            if(m->used+2>=BROWSER_FORM_BYTES) return -28;
            if(space) { m->strings[m->used++]=' '; space=0; }
            m->strings[m->used++]=b;
        }
        if(c->first && !element(c,"script") && !element(c,"style")) c=c->first;
        else { while(c!=n && !c->next) c=c->parent; if(c==n) break; c=c->next; }
    }
    if(m->used>=BROWSER_FORM_BYTES) return -28;
    m->strings[m->used++]=0; return 0;
}
static node *first_id(node *root,const char *id) {
    if(!id || !*id) return NULL;
    for(node *n=root;n;) {
        const char *v=n->kind==1 ? attribute_value(n,"id") : NULL;
        if(v && !strcmp(v,id)) return n;
        if(n->first) n=n->first;
        else { while(n!=root && !n->next) n=n->parent; if(n==root) break; n=n->next; }
    }
    return NULL;
}
static int descendant(node *n,node *ancestor) { for(;n;n=n->parent) if(n==ancestor) return 1; return 0; }
static int disabled(node *n) {
    if(attribute_value(n,"disabled")) return 1;
    for(node *p=n->parent;p;p=p->parent) if(element(p,"fieldset") && attribute_value(p,"disabled")) {
        node *legend=p->first; while(legend && !element(legend,"legend")) legend=legend->next;
        if(!legend || !descendant(n,legend)) return 1;
    }
    return 0;
}
static uint32_t form_owner(node *root,node *n) {
    const char *explicit_owner=attribute_value(n,"form"); node *owner=NULL;
    if(explicit_owner) owner=first_id(root,explicit_owner);
    else {
        owner=n->form;
        if(!owner) for(node *p=n->parent;p;p=p->parent) if(element(p,"form")) { owner=p; break; }
    }
    return owner && element(owner,"form") && owner->form_index ? owner->form_index-1 : BROWSER_FORM_NONE;
}
static int policy(node *n) {
    const char *method=attribute_value(n,"method"),*encoding=attribute_value(n,"enctype"),
        *target=attribute_value(n,"target"),*charset=attribute_value(n,"accept-charset");
    return (method && *method && !equal_ascii(method,"get")) ||
        (encoding && *encoding && !equal_ascii(encoding,"application/x-www-form-urlencoded")) ||
        (target && *target && strcmp(target,"_self")) || (charset && !equal_ascii(charset,"utf-8"));
}
static int unsupported_attributes(node *n) {
    static const char *names[]={"required","pattern","min","max","step","minlength","maxlength",
        "dirname","formaction","formmethod","formenctype","formtarget"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) if(attribute_value(n,names[i])) return 1;
    return 0;
}
int browser_forms_project(node *root,browser_forms_t *m) {
    if(!root || !m) return -22;
    memset(m,0,sizeof(*m)); m->version=BROWSER_FORMS_VERSION; m->used=1;
    int base=0;
    for(unsigned pass=0;pass<3;++pass) for(node *n=root;n;) {
        if(n->kind==1 && n->ns==1) {
            if(!pass) {
                n->control_index=n->form_index=0;
                if(element(n,"base") && (attribute_value(n,"href") || attribute_value(n,"target"))) base=1;
                if(element(n,"form")) {
                    if(m->form_count==BROWSER_FORM_COUNT) return -28;
                    browser_form_t *f=&m->forms[m->form_count++]; n->form_index=m->form_count;
                    f->blocked=(uint32_t)policy(n);
                    if(add_string(m,attribute_value(n,"action"),&f->action,0)) return -28;
                }
            } else if(pass==1) {
                uint32_t kind=0; const char *type=attribute_value(n,"type");
                if(element(n,"input")) {
                    kind=BROWSER_FORM_TEXT;
                    if(equal_ascii(type,"hidden")) kind=BROWSER_FORM_HIDDEN;
                    else if(equal_ascii(type,"checkbox")) kind=BROWSER_FORM_CHECKBOX;
                    else if(equal_ascii(type,"radio")) kind=BROWSER_FORM_RADIO;
                    else if(equal_ascii(type,"submit")) kind=BROWSER_FORM_SUBMIT;
                    else if(equal_ascii(type,"reset")) kind=BROWSER_FORM_RESET;
                    else if(equal_ascii(type,"button")) kind=BROWSER_FORM_BUTTON;
                    else if(type && *type && !equal_ascii(type,"text") && !equal_ascii(type,"search")) kind=BROWSER_FORM_UNSUPPORTED;
                } else if(element(n,"textarea")) kind=BROWSER_FORM_TEXTAREA;
                else if(element(n,"select")) kind=BROWSER_FORM_SELECT;
                else if(element(n,"button")) kind=equal_ascii(type,"reset") ? BROWSER_FORM_RESET :
                    equal_ascii(type,"button") ? BROWSER_FORM_BUTTON : BROWSER_FORM_SUBMIT;
                else if(element(n,"label")) kind=BROWSER_FORM_LABEL;
                else if(element(n,"object") || element(n,"keygen")) kind=BROWSER_FORM_UNSUPPORTED;
                if(kind) {
                    if(m->control_count==BROWSER_FORM_CONTROLS) return -28;
                    browser_form_control_t *c=&m->controls[m->control_count++]; n->control_index=m->control_count;
                    c->kind=kind; c->owner=kind==BROWSER_FORM_LABEL ? BROWSER_FORM_NONE : form_owner(root,n);
                    c->target=BROWSER_FORM_NONE; c->first_option=m->option_count;
                    if(disabled(n)) c->flags|=BROWSER_FORM_DISABLED;
                    for(node *p=n->parent;p;p=p->parent) if(element(p,"datalist")) c->flags|=BROWSER_FORM_DISABLED;
                    if(editable(kind) && attribute_value(n,"readonly")) c->flags|=BROWSER_FORM_READONLY;
                    if((kind==BROWSER_FORM_CHECKBOX || kind==BROWSER_FORM_RADIO) && attribute_value(n,"checked")) c->flags|=BROWSER_FORM_CHECKED;
                    if(unsupported_attributes(n) || kind==BROWSER_FORM_UNSUPPORTED) c->flags|=BROWSER_FORM_BLOCKED;
                    if(attribute_value(n,"multiple")) {
                        if(kind==BROWSER_FORM_SELECT) c->flags|=BROWSER_FORM_MULTIPLE; else c->flags|=BROWSER_FORM_BLOCKED;
                    }
                    const char *v=attribute_value(n,"value");
                    if(!v && (kind==BROWSER_FORM_CHECKBOX || kind==BROWSER_FORM_RADIO)) v="on";
                    if(add_string(m,attribute_value(n,"name"),&c->name,0)) return -28;
                    if(kind==BROWSER_FORM_TEXTAREA) { if(text_content(n,m,&c->value,0)) return -28; }
                    else if(add_string(m,v,&c->value,kind==BROWSER_FORM_TEXT ? 1 : 0)) return -28;
                    if(element(n,"button") || kind==BROWSER_FORM_LABEL) {
                        if(text_content(n,m,&c->label,1)) return -28;
                    } else c->label=c->value;
                    if(!m->strings[c->label] && (kind==BROWSER_FORM_SUBMIT || kind==BROWSER_FORM_RESET))
                        if(add_string(m,kind==BROWSER_FORM_SUBMIT ? "Submit" : "Reset",&c->label,0)) return -28;
                    if(kind==BROWSER_FORM_SELECT) {
                        uint32_t selected=BROWSER_FORM_NONE,first_enabled=BROWSER_FORM_NONE;
                        for(node *o=n->first;o;) {
                            if(element(o,"option")) {
                                if(m->option_count==BROWSER_FORM_OPTIONS) return -28;
                                uint32_t oi=m->option_count++; browser_form_option_t *option=&m->options[oi]; ++c->option_count;
                                if(attribute_value(o,"disabled") || (element(o->parent,"optgroup") && attribute_value(o->parent,"disabled"))) option->flags|=BROWSER_FORM_DISABLED;
                                if(first_enabled==BROWSER_FORM_NONE && !(option->flags&BROWSER_FORM_DISABLED)) first_enabled=oi;
                                if(attribute_value(o,"selected")) {
                                    if(!(c->flags&BROWSER_FORM_MULTIPLE) && selected!=BROWSER_FORM_NONE) m->options[selected].flags&=~BROWSER_FORM_CHECKED;
                                    option->flags|=BROWSER_FORM_CHECKED; selected=oi;
                                }
                                if(text_content(o,m,&option->label,1)) return -28;
                                const char *value=attribute_value(o,"value"); option->value=option->label;
                                if(value && add_string(m,value,&option->value,0)) return -28;
                            }
                            if(o->first && !element(o,"option")) o=o->first;
                            else { while(o!=n && !o->next) o=o->parent; if(o==n) break; o=o->next; }
                        }
                        const char *size=attribute_value(n,"size");
                        if(selected==BROWSER_FORM_NONE && first_enabled!=BROWSER_FORM_NONE && !(c->flags&BROWSER_FORM_MULTIPLE) &&
                           (!size || !*size || !strcmp(size,"0") || !strcmp(size,"1"))) m->options[first_enabled].flags|=BROWSER_FORM_CHECKED;
                    }
                }
            } else if(element(n,"label") && n->control_index) {
                node *target=NULL; const char *id=attribute_value(n,"for");
                if(id) target=first_id(root,id);
                else for(node *c=n->first;c;) {
                    if(c->control_index && !element(c,"label") && m->controls[c->control_index-1].kind!=BROWSER_FORM_HIDDEN) { target=c; break; }
                    if(c->first) c=c->first;
                    else { while(c!=n && !c->next) c=c->parent; if(c==n) break; c=c->next; }
                }
                if(target && target->control_index && !element(target,"label") && m->controls[target->control_index-1].kind!=BROWSER_FORM_HIDDEN)
                    m->controls[n->control_index-1].target=target->control_index-1;
            }
        }
        if(n->first && !element(n,"template") && !element(n,"script") && !element(n,"style")) n=n->first;
        else { while(n!=root && !n->next) n=n->parent; if(n==root) break; n=n->next; }
    }
    if(base) for(uint32_t i=0;i<m->form_count;++i) m->forms[i].blocked=1;
    return browser_forms_validate(m);
}
