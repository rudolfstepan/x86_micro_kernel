#ifndef REIST_TLS_COMPAT_STDIO_H
#define REIST_TLS_COMPAT_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct reist_tls_unsupported_file FILE;
int snprintf(char *buffer, size_t capacity, const char *format, ...);
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list args);

#endif
