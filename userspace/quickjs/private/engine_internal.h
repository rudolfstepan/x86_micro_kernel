#ifndef REIST_JS_ENGINE_INTERNAL_H
#define REIST_JS_ENGINE_INTERNAL_H
/* Private embedding layout, not an SDK ABI; unchanged by the opt-in host. */
#include <reist_js.h>
#include <quickjs.h>
struct reist_js_engine {
    reist_js_config config;
    JSRuntime *runtime;
    JSContext *context;
    size_t used,peak,count;
    uint64_t last,deadline;
    reist_js_status poisoned;
    unsigned running,oom;
};
#endif
