#ifndef REIST_JS_PORT_H
#define REIST_JS_PORT_H
#include <stdint.h>
/* Genuine compiler stack allocation; upstream range/stack checks remain. */
#define alloca(size) __builtin_alloca(size)
/* Explicit embedding seed; not an OS clock or cryptographic entropy API. */
uint64_t reist_js_seed(void *engine);
#endif
