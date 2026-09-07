#include "js_protocol.h"
#include <string.h>
int js_service_header_valid(const js_service_header *h,const js_service_header *q,int reply) {
    if(!h || !q || h->magic!=JS_SERVICE_MAGIC || h->version!=JS_SERVICE_VERSION ||
       h->size!=JS_SERVICE_HEADER || h->reserved[0] || h->reserved[1] ||
       h->operation<JS_OP_HELLO || h->operation>JS_OP_SHUTDOWN ||
       !h->parent_pid || !h->parent_generation || !h->child_pid || !h->child_generation ||
       h->parent_pid==h->child_pid || !h->document || !h->sequence || !h->deadline ||
       h->parent_pid!=q->parent_pid || h->parent_generation!=q->parent_generation ||
       h->child_pid!=q->child_pid || h->child_generation!=q->child_generation ||
       h->document!=q->document || h->sequence!=q->sequence || h->operation!=q->operation ||
       h->deadline!=q->deadline || h->length>JS_SERVICE_DATA || h->offset>h->total ||
       h->length>h->total-h->offset || (!h->length && h->total)) return -84;
    if(!reply) {
        if(h->status!=JS_SERVICE_REQUEST) return -84;
        if(h->operation==JS_OP_HELLO) return h->total==8 ? 0 : -84;
        if(h->operation==JS_OP_EVAL) return h->total<=JS_SERVICE_SOURCE ? 0 : -84;
        return h->total ? -84 : 0;
    }
    if(h->status>6 || (h->status && h->total)) return -84;
    if(h->status) return 0;
    if(h->operation==JS_OP_HELLO) return h->total==16 ? 0 : -84;
    if(h->operation==JS_OP_EVAL) return h->total<JS_SERVICE_RESULT ? 0 : -84;
    if(h->operation==JS_OP_GC || h->operation==JS_OP_HEALTH) return h->total==24 ? 0 : -84;
    return h->total ? -84 : 0;
}
int js_service_accept(const js_service_packet *p,uint32_t length,const js_service_header *q,
    int reply,void *output,uint32_t capacity,js_service_receive *state) {
    if(!p || !state || (reply!=0 && reply!=1) ||
       (state->total!=UINT32_MAX && state->offset==state->total) ||
       length<JS_SERVICE_HEADER || length>sizeof(*p) ||
       js_service_header_valid(&p->header,q,reply) ||
       p->header.length!=length-JS_SERVICE_HEADER || p->header.offset!=state->offset ||
       p->header.total>capacity || (p->header.total && !output) ||
       (state->total!=UINT32_MAX && (state->total!=p->header.total || state->status!=p->header.status))) return -84;
    if(p->header.length) memcpy((uint8_t *)output+state->offset,p->bytes,p->header.length);
    state->offset+=p->header.length; state->total=p->header.total; state->status=p->header.status;
    return 0;
}
void js_service_packet_make(js_service_packet *p,const js_service_header *q,uint32_t status,
    const void *bytes,uint32_t total,uint32_t offset) {
    *p=(js_service_packet){0}; p->header=*q;
    p->header.status=status; p->header.total=total; p->header.offset=offset;
    p->header.length=total-offset;
    if(p->header.length>JS_SERVICE_DATA) p->header.length=JS_SERVICE_DATA;
    if(p->header.length) memcpy(p->bytes,(const uint8_t *)bytes+offset,p->header.length);
}
