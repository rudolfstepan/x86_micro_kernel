#include "script_protocol.h"
int browser_script_message_valid(const browser_script_message_t *m,size_t size,const browser_html_header_t *h,
    uint32_t pid,uint32_t generation,uint32_t ordinal,int reply) {
    if(!m || !h || size<sizeof(*m) || (reply!=0 && reply!=1) ||
       m->magic!=(reply?BROWSER_SCRIPT_REPLY:BROWSER_SCRIPT_MAGIC) || m->version!=1 || m->size!=size ||
       m->request!=h->request || !m->request || m->parent_pid!=h->parent_pid ||
       m->parent_generation!=h->parent_generation || !m->parent_pid || !m->parent_generation ||
       m->child_pid!=pid || m->child_generation!=generation || !pid || !generation || pid==m->parent_pid ||
       m->ordinal!=ordinal || !ordinal || ordinal>BROWSER_SCRIPT_COUNT || m->reserved) return -84;
    if(reply) return !m->snapshot_length && m->source_length<BROWSER_SCRIPT_RESULT &&
        size==sizeof(*m)+m->source_length ? 0 : -84;
    return m->snapshot_length && m->snapshot_length<=BROWSER_SCRIPT_SNAPSHOT &&
        m->source_length<=BROWSER_SCRIPT_SOURCE && size==sizeof(*m)+m->snapshot_length+m->source_length ? 0 : -84;
}
static int nibble(char c) { return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : -1; }
static int word(const char *s,uint32_t *value) {
    uint32_t n=0; for(unsigned i=0;i<8;++i) { int v=nibble(s[i]); if(v<0) return -84; n=(n<<4)|(uint32_t)v; }
    *value=n; return 0;
}
int browser_script_unhex(const char *s,uint32_t length,char *out) {
    for(uint32_t i=0;i<length;++i) {
        int a=nibble(s[2*i]),b=nibble(s[2*i+1]); if(a<0 || b<0) return -84;
        out[i]=(char)((a<<4)|b);
    }
    return 0;
}
int browser_script_journal(const char *data,size_t size,browser_script_mutation_t *items,uint32_t *count,uint32_t *bytes) {
    if(!data || !items || !count || !bytes || size>=BROWSER_SCRIPT_RESULT) return -84;
    uint32_t at=0,n=0,total=0;
    while(at<size) {
        uint32_t id,length;
        if(n==BROWSER_SCRIPT_MUTATIONS || size-at<16 || word(data+at,&id) || word(data+at+8,&length) ||
           id>8192 || length>(size-at-16)/2) return -84;
        at+=16;
        /* Strict UTF-8, including rejection of embedded NUL, overlong forms,
         * surrogate scalars, truncated continuation and out-of-range values. */
        uint32_t scalar=0,minimum=0,left=0;
        for(uint32_t i=0;i<length;++i) {
            int a=nibble(data[at+2*i]),b=nibble(data[at+2*i+1]); if(a<0 || b<0) return -84;
            uint32_t c=(uint32_t)((a<<4)|b);
            if(!left) {
                if(!c) return -84;
                if(c<128) continue;
                if(c>=0xc2 && c<=0xdf) { scalar=c&31; left=1; minimum=128; }
                else if(c>=0xe0 && c<=0xef) { scalar=c&15; left=2; minimum=2048; }
                else if(c>=0xf0 && c<=0xf4) { scalar=c&7; left=3; minimum=65536; }
                else return -84;
            } else {
                if((c&0xc0)!=0x80) return -84;
                scalar=(scalar<<6)|(c&63);
                if(!--left && (scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))) return -84;
            }
        }
        if(left) return -84;
        items[n++]=(browser_script_mutation_t){id,length,at}; at+=length*2; total+=length;
    }
    *count=n; *bytes=total; return 0;
}
