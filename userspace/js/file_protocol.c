#include "file_protocol.h"
#include <string.h>
int js_file_manifest_valid(const js_file_manifest *m) {
    if(!m || m->version!=1 || m->size!=sizeof(*m) || !m->count || m->count>4 || m->reserved) return -84;
    for(uint32_t i=0;i<4;++i) {
        const js_file_grant *g=&m->files[i];
        if(i<m->count) { if(g->slot!=i+1 || !g->lease || g->rights!=JS_FILE_RIGHTS || g->reserved) return -84; }
        else if(g->slot || g->lease || g->rights || g->reserved) return -84;
    }
    return 0;
}
int js_file_source_decode(const void *data,uint32_t size,js_file_manifest *manifest,js_script_source *source) {
    if(!data || !manifest || !source || size<80+24 || size>JS_SERVICE_CAP_SOURCE) return -84;
    js_file_manifest m; js_script_source s; memcpy(&m,data,sizeof(m));
    if(js_file_manifest_valid(&m) || js_script_decode((const char *)data+80,size-80,&s)) return -84;
    *manifest=m; *source=s; return 0;
}
int js_file_request_valid(const void *data,uint32_t size,js_file_request *output) {
    if(!data || !output || size!=32) return -84;
    js_file_request q; memcpy(&q,data,sizeof(q));
    if(q.version!=1 || q.size!=sizeof(q) || !q.call || q.reserved) return -84;
    *output=q; return 0;
}
int js_file_reply_valid(const void *data,uint32_t size,const js_file_request *q) {
    if(!data || !q || size<32 || size>JS_FILE_RESPONSE) return -84;
    js_file_reply r; memcpy(&r,data,sizeof(r));
    if(r.version!=1 || r.size!=sizeof(r) || r.call!=q->call || r.error>0 || r.error< -4095 ||
       r.bytes>JS_FILE_CHUNK || size!=32+r.bytes || r.reserved[0] || r.reserved[1]) return -84;
    if(r.error) return r.bytes || r.value ? -84:0;
    if(q->operation==JS_FILE_READ) return r.bytes>q->argument || r.value ? -84:0;
    if(r.bytes) return -84;
    if(q->operation==JS_FILE_SEEK) return r.value==q->argument?0:-84;
    if(q->operation==JS_FILE_SIZE) return 0;
    return q->operation==JS_FILE_CLOSE && !r.value?0:-84;
}
