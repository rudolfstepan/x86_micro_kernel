#include "script_protocol.h"
#include <string.h>
int js_script_decode(const void *input,uint32_t size,js_script_source *output) {
    js_script_request h;
    if(!input || !output || size<sizeof(h) || size>JS_SERVICE_SCRIPT_SOURCE) return -84;
    memcpy(&h,input,sizeof(h));
    if(h.version!=1 || h.size!=sizeof(h) || h.reserved || !h.argc || h.argc>JS_SCRIPT_ARGC ||
       h.args_bytes<h.argc || h.args_bytes>JS_SCRIPT_ARGS || h.source_bytes>JS_SERVICE_SOURCE ||
       size!=sizeof(h)+h.args_bytes+h.source_bytes) return -84;
    js_script_source decoded={0}; decoded.argc=h.argc; decoded.source_bytes=h.source_bytes;
    const char *args=(const char *)input+sizeof(h);
    uint32_t offset=0;
    for(uint32_t i=0;i<h.argc;++i) {
        if(offset>=h.args_bytes) return -84;
        const char *end=(const char *)memchr(args+offset,0,h.args_bytes-offset);
        if(!end) return -84;
        decoded.argv[i]=args+offset; offset=(uint32_t)(end-args)+1;
    }
    if(offset!=h.args_bytes) return -84;
    decoded.source=args+h.args_bytes;
    if(memchr(decoded.source,0,h.source_bytes)) return -84;
    *output=decoded; return 0;
}
int js_script_reply_valid(const void *input,uint32_t size) {
    js_script_reply h;
    if(!input || size<sizeof(h) || size>sizeof(h)+JS_SCRIPT_CONSOLE) return -84;
    memcpy(&h,input,sizeof(h));
    if(h.version!=1 || h.size!=sizeof(h) || h.status>1 || h.exit_code>125 ||
       (h.status && h.exit_code!=1) || h.records>JS_SCRIPT_RECORDS ||
       h.bytes>JS_SCRIPT_CONSOLE || size!=sizeof(h)+h.bytes) return -84;
    uint32_t offset=sizeof(h);
    for(uint32_t i=0;i<h.records;++i) {
        uint32_t record[2];
        if(size-offset<sizeof(record)) return -84;
        memcpy(record,(const char *)input+offset,sizeof(record)); offset+=sizeof(record);
        if(record[0]<1 || record[0]>2 || !record[1] || record[1]>size-offset) return -84;
        if(((const char *)input)[offset+record[1]-1]!='\n') return -84;
        offset+=record[1];
    }
    return offset==size?0:-84;
}
