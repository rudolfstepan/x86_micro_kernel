/* assert is deliberately re-evaluated on each inclusion (ISO C11 7.2). */
#include <stdlib.h>
#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : abort())
#endif
#ifndef __cplusplus
#define static_assert _Static_assert
#endif
