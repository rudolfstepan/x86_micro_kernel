#ifndef REIST_C_STRING_H
#define REIST_C_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memcpy(void *destination, const void *source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);
void *memchr(const void *data, int value, size_t size);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t size);
char *strchr(const char *text, int value);
char *strrchr(const char *text, int value);
#ifdef __cplusplus
}
#endif
#endif
