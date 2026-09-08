#ifndef REIST_JS_SCRIPT_HOST_H
#define REIST_JS_SCRIPT_HOST_H
#include <reist_js.h>
#ifdef __cplusplus
extern "C" {
#endif
#define REIST_JS_CONSOLE_BYTES (60U*1024U)
#define REIST_JS_CONSOLE_RECORDS 256U
/* Opt-in memory-only host. Storage/host outlive the engine; no threads or
 * reentry. Each record: native i386 uint32 stream,length, then UTF-8 bytes.
 * No write past capacity, no valid prefix after failed=1. Not an OS handle. */
typedef struct {
    uint32_t version,struct_size;
    char *bytes;
    uint32_t capacity,used,records,exit_code,failed,emitting;
} reist_js_script_host;
/* Once, before first untrusted eval on a fresh engine. Installs data directly,
 * never evaluates arguments. Failure poisons the engine; destroy it. */
reist_js_status reist_js_script_attach(reist_js_engine *,reist_js_script_host *,
                                      unsigned argc,const char *const *argv);
#ifdef __cplusplus
}
#endif
#endif
