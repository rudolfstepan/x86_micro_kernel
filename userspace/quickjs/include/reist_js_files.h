#ifndef REIST_JS_FILES_H
#define REIST_JS_FILES_H
#include <reist_js.h>
#ifdef __cplusplus
extern "C" {
#endif
#define REIST_JS_FILE_COUNT 4U
#define REIST_JS_FILE_CHUNK (128U*1024U)
enum { REIST_JS_FILE_READ=1,REIST_JS_FILE_SEEK,REIST_JS_FILE_SIZE,REIST_JS_FILE_CLOSE };
struct reist_js_files_host;
typedef struct {
    struct reist_js_files_host *owner;
    uint32_t slot,lease,rights,closed;
} reist_js_file;
/* Native memory-only adapter; no OS imports. Host/bytes/capabilities outlive
 * the engine. Negative errno from callback, -28 for irreversible quota failure,
 * -84/-110 for irreversible transport/identity/deadline loss. No reentry. */
typedef struct reist_js_files_host {
    uint32_t version,struct_size,count,busy,failed;
    void *context;
    int (*call)(void *,uint32_t slot,uint32_t lease,uint32_t operation,uint32_t argument,
                void *bytes,uint32_t *length,uint32_t *value);
    char *bytes;
    reist_js_file files[REIST_JS_FILE_COUNT];
} reist_js_files_host;
reist_js_status reist_js_files_attach(reist_js_engine *,reist_js_files_host *);
#ifdef __cplusplus
}
#endif
#endif
