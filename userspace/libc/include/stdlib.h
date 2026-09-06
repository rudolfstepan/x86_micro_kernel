#ifndef REIST_C_STDLIB_H
#define REIST_C_STDLIB_H
#include <reist/libc.h>
#ifdef __cplusplus
extern "C" {
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int abs(int value);
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
REIST_LIBC_NORETURN void abort(void);
#ifdef __cplusplus
}
#endif
#endif
