#ifndef REIST_JS_SERVICE_PROTOCOL_H
#define REIST_JS_SERVICE_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JS_SERVICE_MAGIC 0x3157534aU
#define JS_SERVICE_VERSION 1U
#define JS_SERVICE_SOURCE (1024U*1024U)
#define JS_SERVICE_SCRIPT_SOURCE (JS_SERVICE_SOURCE+4096U+24U)
#define JS_SERVICE_CAP_SOURCE (JS_SERVICE_SCRIPT_SOURCE+80U)
#define JS_SERVICE_RESULT 65536U
#define JS_SERVICE_HEAP (32U*1024U*1024U)
#define JS_SERVICE_DEADLINE 5000U
#define JS_SERVICE_REAP 1000U
#define JS_SERVICE_HEADER 72U
#define JS_SERVICE_DATA (2048U-JS_SERVICE_HEADER)
#define JS_SERVICE_REQUEST UINT32_MAX
enum { JS_OP_HELLO=1,JS_OP_EVAL,JS_OP_GC,JS_OP_HEALTH,JS_OP_SHUTDOWN,JS_OP_SCRIPT,JS_OP_CAP_SCRIPT,JS_OP_FILE };
typedef struct js_service_header {
    uint32_t magic,version,size,operation;
    uint32_t parent_pid,parent_generation,child_pid,child_generation;
    uint32_t document,sequence,status,total,offset,length,reserved[2];
    uint64_t deadline;
} js_service_header;
typedef struct js_service_packet { js_service_header header; uint8_t bytes[JS_SERVICE_DATA]; } js_service_packet;
typedef struct js_service_receive { uint32_t offset,total,status; } js_service_receive;
/* total/status start at UINT32_MAX, offset at zero. Output is private staging
 * until the entire command has been accepted; a failure changes no byte/state. */
int js_service_accept(const js_service_packet *,uint32_t wire_length,
    const js_service_header *expected,int reply,void *output,uint32_t capacity,js_service_receive *);
/* All fields other than status/total/offset/length must match the transaction. */
int js_service_header_valid(const js_service_header *,const js_service_header *,int reply);
void js_service_packet_make(js_service_packet *,const js_service_header *,
    uint32_t status,const void *bytes,uint32_t total,uint32_t offset);
#ifdef __cplusplus
}
static_assert(sizeof(js_service_header)==JS_SERVICE_HEADER);
static_assert(sizeof(js_service_packet)==2048);
#else
_Static_assert(sizeof(js_service_header)==JS_SERVICE_HEADER,"JS wire header");
_Static_assert(sizeof(js_service_packet)==2048,"JS bulk frame");
#endif
#endif
