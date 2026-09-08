#include "css_values.hpp"
#include <stdlib.h>
#include <string.h>
#include <libwapcaplet/libwapcaplet.h>

namespace {
constexpr uint32_t DeclarationLimit=4096, TokenLimit=32768, ValueLimit=512, VariableLimit=64;
struct Tokens { browser_css_token *data; uint32_t count; };
struct Declaration { char *name; Tokens tokens; bool custom, deferred; };
struct Priority { uint32_t origin, specificity, important; bool set; };
struct Variable { Declaration *declaration; Priority priority; Tokens resolved; uint8_t mark; bool invalid; };
class Scratch {
    browser_css_token *data_=static_cast<browser_css_token *>(malloc(ValueLimit*sizeof(browser_css_token)));
public:
    Scratch()=default;
    Scratch(const Scratch &)=delete;
    Scratch &operator=(const Scratch &)=delete;
    ~Scratch() { free(data_); }
    browser_css_token *get() const { return data_; }
};
Declaration *declarations[DeclarationLimit];
uint32_t declaration_count, token_count;
int (*charge)();
bool failed;
browser_css_values *current;
bool collecting;
bool variables_needed;
int tick() { if(failed || (charge && charge())) { failed=true; return -28; } return 0; }
const char *bytes(const browser_css_token &t) { return t.string ? lwc_string_data(static_cast<lwc_string *>(t.string)) : ""; }
size_t size(const browser_css_token &t) { return t.string ? lwc_string_length(static_cast<lwc_string *>(t.string)) : 0; }
bool equal(const browser_css_token &t,const char *s,bool insensitive=false) {
    size_t n=strlen(s); if(n!=size(t)) return false;
    for(size_t i=0;i<n;++i) { unsigned c=(unsigned char)bytes(t)[i]; if(insensitive && c>='A' && c<='Z') c+=32; if(c!=(unsigned char)s[i]) return false; }
    return true;
}
bool character(const browser_css_token &t,char c) { return t.type==BC_CHAR && size(t)==1 && *bytes(t)==c; }
bool variable(const browser_css_token &t) { return t.type==BC_FUNCTION && equal(t,"var",true); }
bool custom_name(const char *s,size_t n) { return n>2 && s[0]=='-' && s[1]=='-'; }
bool outranks(Priority &p,uint32_t origin,uint32_t specificity,uint32_t important) {
    /* Same origin ordering as the pinned selector cascade; calls are in its
     * specificity/source order, including matched @media and inline rules. */
    bool wins=!p.set || (origin>p.origin && !(p.origin==1 && p.important)) ||
        (origin<p.origin && origin==1 && important) ||
        (origin==p.origin && (origin==0 ? specificity>=p.specificity :
            important!=p.important ? important>p.important : specificity>=p.specificity));
    if(wins) p={origin,specificity,important,true};
    return wins;
}
void clear(Tokens &t) {
    for(uint32_t i=0;i<t.count;++i) if(t.data[i].string) lwc_string_unref(static_cast<lwc_string *>(t.data[i].string));
    token_count-=t.count; free(t.data); t={};
}
bool copy(Tokens &out,const browser_css_token *in,uint32_t n) {
    if(n>ValueLimit || n>TokenLimit-token_count || tick()) { failed=true; return false; }
    out.data=static_cast<browser_css_token *>(calloc(n ? n : 1,sizeof(*in)));
    if(!out.data) { failed=true; return false; }
    out.count=n; token_count+=n;
    for(uint32_t i=0;i<n;++i) { out.data[i]=in[i]; if(in[i].string) lwc_string_ref(static_cast<lwc_string *>(in[i].string)); }
    return true;
}
/* Balanced component values, using tokens from LibCSS, not another lexer.
 * Strings/URLs remain atomic and token boundaries survive substitution. */
int close_at(const browser_css_token *v,uint32_t begin,uint32_t end,uint32_t &comma) {
    char stack[64]; uint32_t depth=1; stack[0]=')'; comma=end;
    for(uint32_t i=begin+1;i<end;++i) {
        if(tick()) return -1;
        char close= v[i].type==BC_FUNCTION || character(v[i],'(') ? ')' : character(v[i],'[') ? ']' : character(v[i],'{') ? '}' : 0;
        if(close) { if(depth==64) { failed=true; return -1; } stack[depth++]=close; }
        else if(character(v[i],')') || character(v[i],']') || character(v[i],'}')) {
            if(!character(v[i],stack[depth-1])) return -1;
            if(!--depth) return (int)i;
        } else if(depth==1 && comma==end && character(v[i],',')) comma=i;
    }
    return -1;
}
bool valid(const browser_css_token *v,uint32_t n) {
    char stack[64]; uint32_t depth=0;
    for(uint32_t i=0;i<n;++i) {
        if(tick()) return false;
        if(v[i].type==BC_BAD_STRING || v[i].type==BC_EOF || (!depth && (character(v[i],'!') || character(v[i],';')))) return false;
        if(v[i].type==BC_FUNCTION || character(v[i],'(')) {
            uint32_t comma; int end=close_at(v,i,n,comma); if(end<0) return false;
            if(variable(v[i])) {
                uint32_t j=i+1; while(j<(uint32_t)end && v[j].type==BC_SPACE) ++j;
                if(j==(uint32_t)end || v[j].type!=BC_IDENT || !custom_name(bytes(v[j]),size(v[j]))) return false;
                ++j; while(j<(uint32_t)end && v[j].type==BC_SPACE) ++j;
                if(j!=(uint32_t)end && j!=comma) return false;
            }
        }
        char close=v[i].type==BC_FUNCTION || character(v[i],'(') ? ')' : character(v[i],'[') ? ']' : character(v[i],'{') ? '}' : 0;
        if(close) { if(depth==64) { failed=true; return false; } stack[depth++]=close; }
        else if(character(v[i],')') || character(v[i],']') || character(v[i],'}')) {
            if(!depth || !character(v[i],stack[depth-1])) return false;
            --depth;
        }
    }
    return depth==0;
}
}

struct browser_css_values {
    browser_css_values *parent;
    Variable *variables;
    uint32_t count;
    Priority extra_priority[6];
    browser_css_extra extra;
};

namespace {
/* Grammar extensions consume upstream tokens; no reparsing of stylesheet text. */
class Cursor {
public:
    const browser_css_token *tokens; uint32_t count, at=0;
    Cursor(const browser_css_token *p,uint32_t n):tokens(p),count(n) {}
    void spaces() { while(at<count && tokens[at].type==BC_SPACE) ++at; }
    bool end() { spaces(); return at==count; }
    bool ch(char c) { spaces(); if(at<count && character(tokens[at],c)) { ++at; return true; } return false; }
    bool word(const char *s,uint32_t type=BC_IDENT) { spaces(); if(at<count && tokens[at].type==type && equal(tokens[at],s,true)) { ++at; return true; } return false; }
    bool length(browser_css_length &out,bool fr=false,bool negative=false) {
        spaces(); if(at==count || tick()) return false;
        const auto &t=tokens[at];
        if(t.type!=BC_NUMBER && t.type!=BC_DIMENSION && t.type!=BC_PERCENT) return false;
        const char *s=bytes(t); size_t n=size(t),i=0; int sign=1;
        if(i<n && (s[i]=='-' || s[i]=='+')) { if(s[i]=='-') sign=-1; ++i; }
        int64_t number=0,frac=0,denom=1; bool digit=false;
        while(i<n && s[i]>='0' && s[i]<='9') { digit=true; number=number*10+s[i++]-'0'; if(number>262144) return false; }
        if(i<n && s[i]=='.') {
            ++i; while(i<n && s[i]>='0' && s[i]<='9') { digit=true; if(denom<1000000) { denom*=10; frac=frac*10+s[i]-'0'; } ++i; }
        }
        if(!digit || (sign<0 && !negative)) return false;
        uint32_t unit=BC_PX;
        const char *suffix=s+i; size_t left=n-i;
        if(t.type==BC_PERCENT) { if(left && !(left==1 && *suffix=='%')) return false; unit=BC_PERCENT_UNIT; }
        else if(t.type==BC_NUMBER) { if(left || number || frac) return false; }
        else {
            const char *names[]={"px","em","rem","%","fr","vw","vh"}; bool found=false;
            for(uint32_t j=0;j<7;++j) if(strlen(names[j])==left) {
                bool same=true; for(size_t k=0;k<left;++k) { unsigned c=(unsigned char)suffix[k]; if(c>='A' && c<='Z') c+=32; if(c!=(unsigned char)names[j][k]) same=false; }
                if(same) { unit=j; found=true; break; }
            }
            if(!found || (!fr && unit==BC_FR)) return false;
        }
        int64_t value=(number*1024+frac*1024/denom)*sign;
        if(value>262144*1024LL || value< -262144*1024LL) return false;
        out={(int32_t)value,unit}; ++at; return true;
    }
    bool track(browser_css_track &out) {
        if(word("minmax",BC_FUNCTION)) {
            if(!length(out.minimum) || !ch(',') || !length(out.maximum,true) || !ch(')')) return false;
            /* Fixed upper bounds require the separate intrinsic track-sizing
             * algorithm; do not misinterpret them as fixed-width tracks. */
            if(out.maximum.unit!=BC_FR) return false;
        } else {
            if(!length(out.maximum,true)) return false;
            out.minimum=out.maximum.unit==BC_FR ? browser_css_length{0,BC_PX} : out.maximum;
        }
        return true;
    }
};
int extension(const char *name) {
    if(!strcmp(name,"display")) return 0;
    if(!strcmp(name,"row-gap") || !strcmp(name,"gap")) return 1;
    if(!strcmp(name,"column-gap")) return 2;
    if(!strcmp(name,"grid-template-columns")) return 3;
    if(!strcmp(name,"border-radius")) return 4;
    if(!strcmp(name,"box-shadow")) return 5;
    return -1;
}
bool parse_extra(int index,const char *name,const browser_css_token *t,uint32_t n,void *sheet,browser_css_extra &e) {
    Cursor c(t,n);
    if(index==0) { e.grid=c.word("grid") ? 1 : c.word("inline-grid") ? 2 : 0; return !e.grid || c.end(); }
    if(index==1 || index==2) {
        browser_css_length value={}; if(!c.word("normal") && !c.length(value)) return false;
        if(index==2) e.column_gap=value;
        else { e.row_gap=value; if(!strcmp(name,"gap")) { if(!c.end() && !c.length(value)) return false; e.column_gap=value; } }
    } else if(index==3) {
        e.tracks=e.auto_fit=0;
        if(c.word("none")) return c.end();
        while(!c.end()) {
            uint32_t repeat=1; bool repeating=c.word("repeat",BC_FUNCTION);
            if(repeating) {
                if(c.word("auto-fit")) { if(e.tracks || e.auto_fit) return false; e.auto_fit=1; }
                else {
                    c.spaces(); if(c.at==n || t[c.at].type!=BC_NUMBER) return false;
                    repeat=0; for(size_t j=0;j<size(t[c.at]);++j) { char b=bytes(t[c.at])[j]; if(b<'0' || b>'9' || repeat>16) return false; repeat=repeat*10+(uint32_t)(b-'0'); }
                    ++c.at; if(!repeat || repeat>16) return false;
                }
                if(!c.ch(',')) return false;
            }
            browser_css_track track;
            if(!c.track(track) || (repeating && !c.ch(')')) || repeat>16-e.tracks) return false;
            if(e.auto_fit && (track.minimum.unit!=BC_PX || track.minimum.value<=0 || track.maximum.unit!=BC_FR || !track.maximum.value)) return false;
            for(uint32_t i=0;i<repeat;++i) e.columns[e.tracks++]=track;
            if(e.auto_fit && !c.end()) return false;
        }
        return e.tracks!=0;
    } else if(index==4) {
        if(!c.length(e.radius) || e.radius.unit==BC_PERCENT_UNIT) return false; /* uniform circular length, not elliptical percentages */
    } else if(index==5) {
        e.shadow_color=0; e.shadow_x=e.shadow_y=e.shadow_blur=e.shadow_spread=0;
        if(c.word("none")) return c.end();
        browser_css_length lengths[4]={}; uint32_t count=0; bool color=false;
        while(!c.end()) {
            if(count<4 && c.length(lengths[count],false,true)) { if(lengths[count].unit!=BC_PX) return false; ++count; }
            else {
                uint32_t consumed=0;
                if(color || css_reist_parse_color(sheet,t+c.at,n-c.at,&consumed,&e.shadow_color) || !consumed) return false;
                c.at+=consumed; color=true;
            }
        }
        if(count<2 || lengths[2].value<0) return false;
        e.shadow_x=lengths[0].value/1024; e.shadow_y=lengths[1].value/1024;
        e.shadow_blur=lengths[2].value/1024; e.shadow_spread=lengths[3].value/1024;
        if(!color) e.shadow_color=0xff202020;
        if(e.shadow_x< -64 || e.shadow_x>64 || e.shadow_y< -64 || e.shadow_y>64 || e.shadow_blur>32 || e.shadow_spread< -32 || e.shadow_spread>32) return false;
    }
    return c.end();
}
Variable *local(browser_css_values *s,const char *name) {
    for(uint32_t i=0;i<s->count;++i) { if(tick()) return nullptr; if(!strcmp(s->variables[i].declaration->name,name)) return &s->variables[i]; }
    return nullptr;
}
Variable *find(browser_css_values *s,const char *name) {
    for(uint32_t depth=0;s && depth<128;s=s->parent,++depth) { Variable *v=local(s,name); if(v || failed) return v; }
    return nullptr;
}
/* Mark cycles through every var() edge, including unused fallback edges. */
bool graph(Variable &v,Variable **stack,uint32_t depth) {
    if(tick() || depth==VariableLimit) { failed=true; return false; }
    if(v.mark==2) return true;
    if(v.mark==1) { bool cycle=false; for(uint32_t i=0;i<depth;++i) { if(stack[i]==&v) cycle=true; if(cycle) stack[i]->invalid=true; } return true; }
    v.mark=1; stack[depth++]=&v;
    const Tokens &t=v.declaration->tokens;
    for(uint32_t i=0;i<t.count;++i) if(variable(t.data[i])) {
        uint32_t j=i+1; while(j<t.count && t.data[j].type==BC_SPACE) ++j;
        if(j<t.count) {
            Variable *next=local(current,bytes(t.data[j]));
            if(next && !graph(*next,stack,depth)) return false;
        }
    }
    v.mark=2; return true;
}
bool resolve(Variable &,uint32_t);
bool expand(const browser_css_token *v,uint32_t begin,uint32_t end,browser_css_token *out,uint32_t &used,uint32_t depth) {
    if(depth>=64 || tick()) { failed=true; return false; }
    for(uint32_t i=begin;i<end;++i) {
        if(tick()) return false;
        if(variable(v[i])) {
            uint32_t comma; int close=close_at(v,i,end,comma); if(close<0) return false;
            uint32_t name=i+1; while(name<(uint32_t)close && v[name].type==BC_SPACE) ++name;
            if(name==(uint32_t)close) return false;
            Variable *value=find(current,bytes(v[name]));
            if(value && !resolve(*value,depth+1)) return false;
            if(value && !value->invalid) {
                for(uint32_t j=0;j<value->resolved.count;++j) {
                    if(used==ValueLimit || tick()) { failed=true; return false; }
                    out[used++]=value->resolved.data[j];
                }
            } else {
                if(comma==(uint32_t)end || !expand(v,comma+1,(uint32_t)close,out,used,depth+1)) return false;
            }
            i=(uint32_t)close;
        } else { if(used==ValueLimit) { failed=true; return false; } out[used++]=v[i]; }
    }
    return true;
}
bool resolve(Variable &v,uint32_t depth) {
    if(v.mark==4 || v.invalid) return true;
    if(v.mark==3) { v.invalid=true; return true; }
    if(depth>=64 || tick()) { failed=true; return false; }
    v.mark=3;
    const Tokens &t=v.declaration->tokens;
    uint32_t first=0,last=t.count; while(first<last && t.data[first].type==BC_SPACE) ++first;
    while(last>first && t.data[last-1].type==BC_SPACE) --last;
    bool keyword=last-first==1 && t.data[first].type==BC_IDENT;
    if(keyword && equal(t.data[first],"initial",true)) v.invalid=true;
    else if(keyword && (equal(t.data[first],"inherit",true) || equal(t.data[first],"unset",true))) {
        Variable *parent=find(current->parent,v.declaration->name);
        if(!parent || parent->invalid) v.invalid=true;
        else if(!copy(v.resolved,parent->resolved.data,parent->resolved.count)) return false;
    } else {
        Scratch scratch; uint32_t used=0;
        if(!scratch.get()) { failed=true; return false; }
        if(!expand(t.data,0,t.count,scratch.get(),used,depth+1)) v.invalid=true;
        else if(!copy(v.resolved,scratch.get(),used)) return false;
    }
    v.mark=4; return !failed;
}
}

extern "C" void browser_css_values_reset(int (*budget)()) {
    /* Previous generation cleanup must precede the next allocator reset. */
    declaration_count=token_count=0; current=nullptr; charge=budget; failed=false; collecting=true; variables_needed=false;
}
extern "C" int browser_css_values_active() { return charge!=nullptr; }
extern "C" int browser_css_values_collecting() { return collecting; }
extern "C" void browser_css_values_release() {
    while(declaration_count) { Declaration *d=declarations[--declaration_count]; clear(d->tokens); free(d->name); free(d); }
    current=nullptr; charge=nullptr;
}
extern "C" int browser_css_values_capture(const char *name,size_t length,const browser_css_token *tokens,uint32_t n,uint32_t *id,uint32_t *important) {
    if(!charge) return 0;
    bool custom=custom_name(name,length), deferred=false;
    for(uint32_t i=0;i<n;++i) { if(tick()) return -28; if(variable(tokens[i])) deferred=true; }
    char canonical[257];
    if(length>256) { if(custom || deferred) { failed=true; return -28; } return 0; }
    for(size_t i=0;i<length;++i) { unsigned c=(unsigned char)name[i]; if(!custom && c>='A' && c<='Z') c+=32; canonical[i]=(char)c; }
    canonical[length]=0;
    if(!custom && !deferred && extension(canonical)<0) return 0;
    if(custom || deferred) variables_needed=true;
    *important=0;
    while(n && tokens[n-1].type==BC_SPACE) --n;
    if(n && tokens[n-1].type==BC_IDENT && equal(tokens[n-1],"important",true)) {
        uint32_t j=n-1; while(j && tokens[j-1].type==BC_SPACE) --j;
        if(j && character(tokens[j-1],'!')) { n=j-1; *important=1; }
    }
    if(!valid(tokens,n)) return failed ? -28 : -84;
    if(declaration_count==DeclarationLimit || n>ValueLimit || length>256 || tick()) { failed=true; return -28; }
    auto *d=static_cast<Declaration *>(calloc(1,sizeof(Declaration)));
    if(!d) { failed=true; return -28; }
    d->name=static_cast<char *>(malloc(length+1));
    if(!d->name || !copy(d->tokens,tokens,n)) { clear(d->tokens); free(d->name); free(d); failed=true; return -28; }
    memcpy(d->name,canonical,length+1); d->custom=custom; d->deferred=deferred;
    *id=declaration_count; declarations[declaration_count++]=d; return 1;
}
extern "C" int browser_css_values_begin(browser_css_values **out,browser_css_values *parent) {
    if(tick()) return -28;
    current=static_cast<browser_css_values *>(calloc(1,sizeof(*current)));
    if(!current) { failed=true; return -28; }
    current->parent=parent; *out=current; collecting=variables_needed; return 0;
}
extern "C" int browser_css_values_resolve() {
    Variable *stack[VariableLimit];
    for(uint32_t i=0;i<current->count;++i) if(!graph(current->variables[i],stack,0)) return -28;
    for(uint32_t i=0;i<current->count;++i) if(!resolve(current->variables[i],0)) return -28;
    collecting=false; return failed ? -28 : 0;
}
extern "C" void browser_css_values_destroy(browser_css_values *s) {
    if(!s) return;
    for(uint32_t i=0;i<s->count;++i) clear(s->variables[i].resolved);
    free(s->variables); free(s);
}
extern "C" const browser_css_extra *browser_css_values_extra(const browser_css_values *s) {
    static const browser_css_extra empty={}; return s ? &s->extra : &empty;
}
extern "C" int browser_css_values_apply(uint32_t id,uint32_t origin,uint32_t specificity,uint32_t important,uint32_t pseudo,void *sheet,void **style) {
    *style=nullptr;
    if(!current || id>=declaration_count || tick()) return -28;
    Declaration &d=*declarations[id];
    if(pseudo) return 0; /* Generated content/pseudo-element layout not admitted. */
    if(d.custom) {
        if(!collecting) return 0;
        Variable *v=local(current,d.name);
        if(!v) {
            if(current->count==VariableLimit || failed) { failed=true; return -28; }
            if(!current->variables) current->variables=static_cast<Variable *>(calloc(VariableLimit,sizeof(Variable)));
            if(!current->variables) { failed=true; return -28; }
            v=&current->variables[current->count++];
        }
        if(outranks(v->priority,origin,specificity,important)) v->declaration=&d;
        return 0;
    }
    if(collecting) return 0;
    browser_css_token out[ValueLimit]; uint32_t used=0;
    bool valid_value=expand(d.tokens.data,0,d.tokens.count,out,used,0);
    if(failed) return -28;
    int ext=extension(d.name);
    if(ext>=0) {
        browser_css_extra parsed=current->extra;
        bool accepted=valid_value && parse_extra(ext,d.name,out,used,sheet,parsed);
        if(ext==0 && accepted && !parsed.grid) {
            int rc=css_reist_parse_value(sheet,d.name,strlen(d.name),out,used,important,style);
            if(rc==-28) return rc;
            if(rc==-84) accepted=false;
        }
        if(!accepted && !d.deferred) return 0; /* Invalid at parse time: no cascade effect. */
        if(!accepted) parsed={}; /* Deferred invalid value computes to initial. */
        if(outranks(current->extra_priority[ext],origin,specificity,important)) {
            if(ext==0) current->extra.grid=parsed.grid;
            else if(ext==1) current->extra.row_gap=parsed.row_gap;
            else if(ext==2) current->extra.column_gap=parsed.column_gap;
            else if(ext==3) { current->extra.tracks=parsed.tracks; current->extra.auto_fit=parsed.auto_fit; memcpy(current->extra.columns,parsed.columns,sizeof(parsed.columns)); }
            else if(ext==4) current->extra.radius=parsed.radius;
            else { current->extra.shadow_x=parsed.shadow_x; current->extra.shadow_y=parsed.shadow_y; current->extra.shadow_blur=parsed.shadow_blur; current->extra.shadow_spread=parsed.shadow_spread; current->extra.shadow_color=parsed.shadow_color; }
        }
        if(ext==1 && !strcmp(d.name,"gap") && outranks(current->extra_priority[2],origin,specificity,important)) current->extra.column_gap=parsed.column_gap;
        if(ext!=0) return failed ? -28 : 0;
        if(parsed.grid) {
            lwc_string *display=nullptr; const char *value=parsed.grid==1 ? "block" : "inline-block";
            if(lwc_intern_string(value,strlen(value),&display)!=lwc_error_ok) return -28;
            browser_css_token token={BC_IDENT,display};
            int rc=css_reist_parse_value(sheet,"display",7,&token,1,important,style);
            lwc_string_unref(display); return rc;
        }
        if(*style) return 0;
    }
    int result=valid_value ? css_reist_parse_value(sheet,d.name,strlen(d.name),out,used,important,style) : -84;
    if(result==-84 && d.deferred) {
        lwc_string *unset=nullptr;
        if(lwc_intern_string("unset",5,&unset)!=lwc_error_ok) return -28;
        browser_css_token token={BC_IDENT,unset};
        result=css_reist_parse_value(sheet,d.name,strlen(d.name),&token,1,important,style);
        lwc_string_unref(unset);
    }
    return result==-84 ? 0 : result;
}
