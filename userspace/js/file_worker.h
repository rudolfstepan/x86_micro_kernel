#ifndef REIST_JS_FILE_WORKER_H
#define REIST_JS_FILE_WORKER_H
#include "file_protocol.h"
#include <reist_js_files.h>
typedef struct {
    uint32_t incoming,outgoing,calls,bytes;
    uint64_t last;
    js_service_header header;
    char *staging;
} js_file_bridge;
int js_file_worker_call(void *,uint32_t,uint32_t,uint32_t,uint32_t,void *,uint32_t *,uint32_t *);
#endif
