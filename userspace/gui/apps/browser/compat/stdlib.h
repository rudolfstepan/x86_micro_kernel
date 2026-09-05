#ifndef REIST_BROWSER_COMPAT_STDLIB_H
#define REIST_BROWSER_COMPAT_STDLIB_H
#include <stddef.h>
static inline int abs(int value) { return value < 0 ? -value : value; }
#endif
