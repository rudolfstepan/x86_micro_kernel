#ifndef REIST_TEXT_STDIO_H
#define REIST_TEXT_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Opt-in ISO C string formatting, fixed C locale. No FILE/console authority. */
int snprintf(char *destination, size_t capacity, const char *format, ...)
    __attribute__((format(printf,3,4)));
int vsnprintf(char *destination, size_t capacity, const char *format, va_list args)
    __attribute__((format(printf,3,0)));
#ifdef __cplusplus
}
#endif
#endif
