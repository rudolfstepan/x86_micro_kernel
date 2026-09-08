#include "file_worker.h"
#include "x86os.h"
#include <string.h>
static int remaining(js_file_bridge *b,uint32_t *ms) {
    uint64_t now=0;x86os_process_identity_t p;
    if(x86os_monotonic_ms(&now) || now<b->last || now>=b->header.deadline)return -110;
    b->last=now;
    if(x86os_process_identity_of((int)b->header.parent_pid,&p) || p.version!=1 ||
       p.struct_size!=sizeof(p) || p.pid!=(int)b->header.parent_pid || p.generation!=b->header.parent_generation)return -84;
    *ms=(uint32_t)(b->header.deadline-now);return 0;
}
int js_file_worker_call(void *context,uint32_t slot,uint32_t lease,uint32_t operation,
                        uint32_t argument,void *bytes,uint32_t *length,uint32_t *value) {
    js_file_bridge *b=(js_file_bridge *)context;*length=*value=0;
    if(!b || !b->staging)return -84;
    if(b->calls>=JS_FILE_CALLS || (operation==JS_FILE_READ && argument>JS_FILE_BYTES-b->bytes))return -28;
    js_file_request q={1,sizeof(q),++b->calls,slot,lease,operation,argument,0};
    js_service_header header=b->header;header.operation=JS_OP_FILE;
    js_service_packet packet;js_service_packet_make(&packet,&header,JS_SERVICE_REQUEST,&q,sizeof(q),0);
    x86os_ipc_bulk_message_t message={X86OS_IPC_BULK_MESSAGE_VERSION,sizeof(message),JS_SERVICE_HEADER+sizeof(q),{0}};
    memcpy(message.payload,&packet,message.length);uint32_t ms=0;
    int rc=remaining(b,&ms);if(rc)return rc;
    if(x86os_ipc_send_bulk_timeout(b->outgoing,&message,ms))return -84;
    js_service_receive received={0,UINT32_MAX,UINT32_MAX};
    do {
        rc=remaining(b,&ms);if(rc)return rc;
        message.length=0;
        rc=x86os_ipc_receive_bulk_timeout(b->incoming,&message,ms);
        if(rc)return rc==-110?-110:-84;
        if(message.version!=X86OS_IPC_BULK_MESSAGE_VERSION || message.struct_size!=sizeof(message))return -84;
        memcpy(&packet,message.payload,sizeof(packet));
        if(js_service_accept(&packet,message.length,&header,1,b->staging,JS_FILE_RESPONSE,&received) || received.status)return -84;
    }while(received.offset!=received.total);
    if(remaining(b,&ms) || js_file_reply_valid(b->staging,received.total,&q))return -84;
    js_file_reply r;memcpy(&r,b->staging,sizeof(r));if(r.error)return r.error;
    if(r.bytes)memcpy(bytes,b->staging+sizeof(r),r.bytes);
    b->bytes+=r.bytes;*length=r.bytes;*value=r.value;return 0;
}
