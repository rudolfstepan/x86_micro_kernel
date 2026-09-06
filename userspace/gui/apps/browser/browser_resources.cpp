#include "browser_resources.hpp"
extern "C" {
#include "../../../programs/curl_http.h"
#include <string.h>
}

static uint32_t url_length(const char *s) {
    if(s) for(unsigned i=0;i<BROWSER_RESOURCE_URL_CAPACITY;++i) if(!s[i]) return i;
    return BROWSER_RESOURCE_URL_CAPACITY;
}
static int terminated(const char *s) { return url_length(s)<BROWSER_RESOURCE_URL_CAPACITY; }
/* URL identity is always bounded, including malformed incoming fixed fields.
 * Use the shared byte contract supported by both legacy chrome and libc; do
 * not introduce another general C-string runtime into the browser. */
static int url_equal(const char *a,const char *b) {
    uint32_t n=url_length(a);
    return n<BROWSER_RESOURCE_URL_CAPACITY && n==url_length(b) && !memcmp(a,b,n);
}
static char lower(char c) { return c>='A' && c<='Z' ? (char)(c+'a'-'A') : c; }
static int hex(char c) {
    c=lower(c); return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1;
}
static int scheme(const char *s) {
    if (!s) return 0;
    const char *p="https://"; unsigned i=0;
    while (p[i] && s[i] && lower(s[i])==p[i]) ++i;
    if (!p[i]) return 2;
    p="http://"; i=0;
    while (p[i] && s[i] && lower(s[i])==p[i]) ++i;
    return !p[i] ? 1 : 0;
}
int browser_resource_url(const char *base,const char *reference,char out[BROWSER_RESOURCE_URL_CAPACITY]) {
    if (!base || !reference || !out) return -22;
    size_t n=0; while(n<BROWSER_RESOURCE_URL_CAPACITY && reference[n]) ++n;
    if(n==BROWSER_RESOURCE_URL_CAPACITY) return -90;
    for(size_t i=0;i<n;++i) if((uint8_t)reference[i]<=32 || reference[i]=='\\' || (uint8_t)reference[i]==127) return -22;
    static char normalized[BROWSER_RESOURCE_URL_CAPACITY]; size_t used=0;
    for(size_t i=0;i<n;++i) {
        if(reference[i]!='%') { normalized[used++]=reference[i]; continue; }
        if(i+2>=n || hex(reference[i+1])<0 || hex(reference[i+2])<0) return -22;
        unsigned value=(unsigned)(hex(reference[i+1])*16+hex(reference[i+2]));
        /* RFC 3986 6.2.2: decode only unreserved characters. Encoded slashes,
         * query separators and all other reserved octets retain their meaning. */
        if((value>='a' && value<='z') || (value>='A' && value<='Z') ||
            (value>='0' && value<='9') || value=='-' || value=='.' || value=='_' || value=='~')
            normalized[used++]=(char)value;
        else {
            const char digits[]="0123456789ABCDEF";
            normalized[used++]='%'; normalized[used++]=digits[value>>4]; normalized[used++]=digits[value&15];
        }
        i+=2;
    }
    normalized[used]=0;
    static reist_html_url_workspace_t resolver;
    if(reist_html_url_resolve_wide(base,normalized,out,BROWSER_RESOURCE_URL_CAPACITY,&resolver)) return -22;
    for(unsigned i=0;out[i];++i) if(out[i]=='#') { out[i]=0; break; }
    int network=scheme(out);
    if(network) {
        static reist_curl_url_t parsed;
        if(reist_curl_parse_http_url(out,&parsed)) return -22;
        unsigned begin=network==2 ? 8U : 7U, end=begin;
        while(out[end] && out[end]!='/') ++end;
        for(unsigned i=0;i<end;++i) out[i]=lower(out[i]);
        unsigned port=begin; while(port<end && out[port]!=':') ++port;
        if(port<end && parsed.port==(network==2 ? 443 : 80)) {
            /* Drop a validated default port; destination precedes source. */
            while(out[end]) out[port++]=out[end++]; out[port]=0;
        }
    } else {
        if(out[0]!='/' || out[1]=='/') return -13;
        for(unsigned i=0;out[i] && out[i]!='?';++i) if(out[i]==':') return -13;
    }
    return 0;
}
int browser_resource_admit(const char *document,const char *url) {
    static char canonical[BROWSER_RESOURCE_URL_CAPACITY];
    if(!terminated(url) || browser_resource_url(document,url,canonical) || !url_equal(canonical,url)) return -13;
    int from=scheme(document), to=scheme(url);
    if((from && !to) || (from==2 && to!=2)) return -13;
    return 0;
}
void browser_resources_init(browser_resources_t *b,uint32_t generation) {
    /* Only count entries are live or serializable. Each new entry is cleared
     * on admission; resetting an empty bundle need not touch its maximum pool. */
    memset(b,0,BROWSER_RESOURCE_HEADER_BYTES);
    b->version=BROWSER_RESOURCE_VERSION; b->generation=generation;
}
int browser_resources_find(const browser_resources_t *b,const char *url) {
    if(!b || b->count>BROWSER_RESOURCE_COUNT || !url) return -1;
    for(uint32_t i=0;i<b->count;++i) if(url_equal(b->entries[i].url,url) ||
        (b->entries[i].ready && url_equal(b->entries[i].effective,url))) return (int)i;
    return -1;
}
int browser_resources_add(browser_resources_t *b,const char *document,const char *url,uint32_t depth) {
    if(!b || depth>BROWSER_RESOURCE_DEPTH || browser_resource_admit(document,url)) return -13;
    int old=browser_resources_find(b,url); if(old>=0) return old;
    if(b->count>=BROWSER_RESOURCE_COUNT) return -28;
    browser_resource_t *r=&b->entries[b->count]; memset(r,0,sizeof(*r));
    memcpy(r->url,url,url_length(url)+1); r->depth=depth;
    return (int)b->count++;
}
int browser_resources_store(browser_resources_t *b,uint32_t i,const char *effective,const uint8_t *bytes,uint32_t length) {
    if(!b || i>=b->count || b->count>BROWSER_RESOURCE_COUNT || b->entries[i].ready ||
        length>BROWSER_RESOURCE_LIMIT || b->length>BROWSER_RESOURCE_BYTES ||
        length>BROWSER_RESOURCE_BYTES-b->length || (length && !bytes) ||
        browser_resource_admit(b->entries[i].url,effective)) return -22;
    for(uint32_t j=0;j<i;++j) if(!b->entries[j].ready) return -22;
    browser_resource_t *r=&b->entries[i];
    r->offset=b->length; r->length=length;
    if(length) memcpy(b->bytes+b->length,bytes,length);
    memcpy(r->effective,effective,url_length(effective)+1);
    b->length+=length; r->ready=1;
    return 0;
}
namespace {
int validate_snapshot(const browser_resources_t *b,const char *document,uint32_t generation) {
    if(!b || b->version!=BROWSER_RESOURCE_VERSION || !generation || b->generation!=generation ||
        b->count>BROWSER_RESOURCE_COUNT || b->length>BROWSER_RESOURCE_BYTES) return -84;
    uint32_t end=0, pending=0;
    for(uint32_t i=0;i<b->count;++i) {
        const browser_resource_t *r=&b->entries[i];
        if(r->depth>BROWSER_RESOURCE_DEPTH || r->ready>1 || browser_resource_admit(document,r->url)) return -84;
        for(uint32_t j=0;j<i;++j) if(url_equal(r->url,b->entries[j].url)) return -84;
        if(r->ready) {
            if(pending || r->offset!=end || r->length>BROWSER_RESOURCE_LIMIT || r->length>b->length-end ||
                browser_resource_admit(r->url,r->effective)) return -84;
            end+=r->length;
        } else { if(r->offset || r->length || r->effective[0]) return -84; pending=1; }
    }
    return end==b->length ? 0 : -84;
}
} // namespace
namespace reist::browser {
Result<ValidatedResources,int> ValidatedResources::open(
    const browser_resources_t *bundle,const char *document,uint32_t generation) noexcept {
    using Admission=Result<ValidatedResources,int>;
    int status=validate_snapshot(bundle,document,generation);
    if(status) return Admission::failure(status);
    return Admission::success(Key{},bundle);
}
}
int browser_resources_validate(const browser_resources_t *b,const char *document,uint32_t generation) {
    auto admitted=reist::browser::ValidatedResources::open(b,document,generation);
    return admitted ? 0 : *admitted.error_if();
}
int browser_resources_pack(const browser_resources_t *b,const char *document,uint8_t *out,uint32_t capacity) {
    if(!out) return -84;
    auto admitted=reist::browser::ValidatedResources::open(b,document,b ? b->generation : 0);
    if(!admitted) return -84;
    const auto& snapshot=admitted.value_if()->snapshot();
    uint32_t prefix=BROWSER_RESOURCE_HEADER_BYTES+snapshot.count*sizeof(snapshot.entries[0]);
    uint32_t size=prefix+snapshot.length;
    if(size>capacity) return -28;
    memcpy(out,&snapshot,prefix);
    memcpy(out+prefix,snapshot.bytes,snapshot.length);
    return (int)size;
}
int browser_resources_unpack(const uint8_t *in,uint32_t length,const char *document,browser_resources_t *out) {
    if(!in || !out || length<BROWSER_RESOURCE_HEADER_BYTES || length>sizeof(*out)) return -84;
    uint32_t header[4]; memcpy(header,in,sizeof(header));
    if((header[0]!=2U && header[0]!=BROWSER_RESOURCE_VERSION) || !header[1] || header[2]>BROWSER_RESOURCE_COUNT ||
        header[3]>BROWSER_RESOURCE_BYTES) return -84;
    const uint32_t record=header[0]==2U ? 528U : sizeof(out->entries[0]);
    uint32_t prefix=BROWSER_RESOURCE_HEADER_BYTES+header[2]*record;
    if(length!=prefix+header[3]) return -84;
    browser_resources_init(out,header[1]);
    out->count=header[2]; out->length=header[3];
    if(header[0]==2U) for(uint32_t i=0;i<header[2];++i) {
        const uint8_t *p=in+BROWSER_RESOURCE_HEADER_BYTES+i*record;
        if(!memchr(p+16,0,256) || !memchr(p+272,0,256)) return -84;
        memset(&out->entries[i],0,sizeof(out->entries[i]));
        memcpy(&out->entries[i],p,16);
        memcpy(out->entries[i].url,p+16,256); memcpy(out->entries[i].effective,p+272,256);
    } else memcpy(out->entries,in+BROWSER_RESOURCE_HEADER_BYTES,prefix-BROWSER_RESOURCE_HEADER_BYTES);
    memcpy(out->bytes,in+prefix,header[3]);
    return browser_resources_validate(out,document,out->generation);
}
int browser_resource_need_add(browser_resource_needs_t *n,const char *url,uint32_t depth) {
    if(!n || !terminated(url) || depth>BROWSER_RESOURCE_DEPTH) return -28;
    for(uint32_t i=0;i<n->count && i<BROWSER_RESOURCE_COUNT;++i) if(url_equal(n->items[i].url,url)) return 0;
    if(n->count>=BROWSER_RESOURCE_COUNT) return -28;
    memset(&n->items[n->count],0,sizeof(n->items[n->count]));
    n->items[n->count].depth=depth;
    memcpy(n->items[n->count++].url,url,url_length(url)+1); return 0;
}
int browser_resource_needs_validate(const browser_resource_needs_t *n,uint32_t length,
    const browser_html_header_t *request,uint32_t pid,uint32_t generation,const browser_resources_t *b,const char *document) {
    if(!n || !request || !b || !pid || !generation || length<offsetof(browser_resource_needs_t,items) ||
        n->magic!=BROWSER_RESOURCE_NEED_MAGIC || n->version!=BROWSER_RESOURCE_VERSION ||
        n->size!=length || n->generation!=b->generation || !n->count || n->count>BROWSER_RESOURCE_COUNT ||
        length!=offsetof(browser_resource_needs_t,items)+n->count*sizeof(n->items[0])) return -84;
    browser_html_header_t expected=*request; expected.child_pid=pid; expected.child_generation=generation;
    if(memcmp(&expected,&n->identity,sizeof(expected))) return -84;
    for(uint32_t i=0;i<n->count;++i) {
        if(n->items[i].depth>BROWSER_RESOURCE_DEPTH ||
            browser_resource_admit(document,n->items[i].url) || browser_resources_find(b,n->items[i].url)>=0) return -84;
        for(uint32_t j=0;j<i;++j) if(url_equal(n->items[i].url,n->items[j].url)) return -84;
    }
    return 0;
}
