#ifndef REIST_JS_SCRIPT_PROTOCOL_H
#define REIST_JS_SCRIPT_PROTOCOL_H
#include "js_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JS_SCRIPT_ARGS 4096U
#define JS_SCRIPT_ARGC 16U
#define JS_SCRIPT_HEADER 24U
#define JS_SCRIPT_CONSOLE (60U*1024U)
#define JS_SCRIPT_RECORDS 256U
typedef struct { uint32_t version,size,argc,args_bytes,source_bytes,reserved; } js_script_request;
typedef struct { uint32_t version,size,status,exit_code,records,bytes; } js_script_reply;
typedef struct {
    const char *argv[JS_SCRIPT_ARGC];
    const char *source;
    uint32_t argc,source_bytes;
} js_script_source;
/* Full validation, no output mutation on failure. All views borrow input. */
int js_script_decode(const void *,uint32_t,js_script_source *);
int js_script_reply_valid(const void *,uint32_t);
#ifdef __cplusplus
}
static_assert(sizeof(js_script_request)==24 && sizeof(js_script_reply)==24);
#else
_Static_assert(sizeof(js_script_request)==24 && sizeof(js_script_reply)==24,"script header");
#endif
#endif
