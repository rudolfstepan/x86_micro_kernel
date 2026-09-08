#ifndef REIST_JS_FILE_PROTOCOL_H
#define REIST_JS_FILE_PROTOCOL_H
#include "script_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JS_FILE_COUNT 4U
#define JS_FILE_CHUNK (128U*1024U)
#define JS_FILE_BYTES (16U*1024U*1024U)
#define JS_FILE_CALLS 256U
#define JS_FILE_RIGHTS 7U
#define JS_FILE_RESPONSE (32U+JS_FILE_CHUNK)
enum { JS_FILE_READ=1,JS_FILE_SEEK,JS_FILE_SIZE,JS_FILE_CLOSE };
typedef struct { uint32_t slot,lease,rights,reserved; } js_file_grant;
typedef struct { uint32_t version,size,count,reserved; js_file_grant files[JS_FILE_COUNT]; } js_file_manifest;
typedef struct { uint32_t version,size,call,slot,lease,operation,argument,reserved; } js_file_request;
typedef struct { uint32_t version,size,call; int32_t error; uint32_t bytes,value,reserved[2]; } js_file_reply;
int js_file_manifest_valid(const js_file_manifest *);
int js_file_source_decode(const void *,uint32_t,js_file_manifest *,js_script_source *);
int js_file_request_valid(const void *,uint32_t,js_file_request *);
int js_file_reply_valid(const void *,uint32_t,const js_file_request *);
#ifdef __cplusplus
}
static_assert(sizeof(js_file_manifest)==80 && sizeof(js_file_request)==32 && sizeof(js_file_reply)==32);
#else
_Static_assert(sizeof(js_file_manifest)==80 && sizeof(js_file_request)==32 && sizeof(js_file_reply)==32,"file wire");
#endif
#endif
