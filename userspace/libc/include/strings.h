#ifndef REIST_C_STRINGS_H
#define REIST_C_STRINGS_H
#include <stddef.h>
/* POSIX strncasecmp, explicitly the ASCII C locale. */
#ifdef __cplusplus
extern "C" {
#endif
int strncasecmp(const char *left, const char *right, size_t size);
#ifdef __cplusplus
}
#endif
#endif
