#ifndef REIST_JS_CORE_H
#define REIST_JS_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define REIST_JS_VERSION 1U
#define REIST_JS_SOURCE_MAX (1024U*1024U)
#define REIST_JS_RESULT_MAX (64U*1024U)
typedef struct reist_js_engine reist_js_engine;
typedef enum {
    REIST_JS_OK=0, REIST_JS_EXCEPTION=1, REIST_JS_OOM=2,
    REIST_JS_DEADLINE=3, REIST_JS_LIMIT=4, REIST_JS_INVALID=5, REIST_JS_CLOSED=6
} reist_js_status;
typedef struct {
    uint32_t version,struct_size,memory_limit,stack_limit,source_limit,result_limit,job_limit,reserved;
    uint64_t seed;
    void *clock_context;
    /* Monotonic milliseconds; nonzero failure or regression fails closed.
     * Callback must not block/reenter this engine; context outlives owner. */
    int (*monotonic_ms)(void *,uint64_t *);
} reist_js_config;
typedef struct {
    uint32_t version,struct_size,live_allocations,live_bytes,peak_bytes,poisoned;
} reist_js_stats;
/* Explicit libc backing must already exist, and rounding must be nearest.
 * A process owns its engine(s), with no threads/reentrant calls. No I/O or
 * automatic OS allocation setup. Ordinary C pointer/lifetime rules apply. */
reist_js_engine *reist_js_create(const reist_js_config *,reist_js_status *);
/* Exact source bytes (no embedded NUL), UTF-8 result including trailing NUL.
 * required excludes NUL. Never returns success with truncated output.
 * deadline is absolute monotonic ms; jobs and result conversion share it.
 * Resource/deadline failure closes evaluation until destroy/fresh create. */
reist_js_status reist_js_eval(reist_js_engine *,const char *,size_t,uint64_t,
                             char *,size_t,size_t *required);
reist_js_status reist_js_collect(reist_js_engine *,uint64_t deadline);
int reist_js_get_stats(const reist_js_engine *,reist_js_stats *);
/* Consumes and nulls owner; repeating with NULL is harmless. External owner
 * must still kill/reap a hung/corrupt process; GC is not a watchdog. */
void reist_js_destroy(reist_js_engine **owner);
#ifdef __cplusplus
}
#endif
#endif
