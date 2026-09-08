#ifndef REIST_FONT_STDLIB_H
#define REIST_FONT_STDLIB_H
/* FreeType's documented private platform override, not a public libc ABI. */
#define FTSTDLIB_H_
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define ft_ptrdiff_t ptrdiff_t
#define FT_CHAR_BIT CHAR_BIT
#define FT_USHORT_MAX USHRT_MAX
#define FT_INT_MAX INT_MAX
#define FT_INT_MIN INT_MIN
#define FT_UINT_MAX UINT_MAX
#define FT_LONG_MIN LONG_MIN
#define FT_LONG_MAX LONG_MAX
#define FT_ULONG_MAX ULONG_MAX
#define FT_LLONG_MAX LLONG_MAX
#define FT_LLONG_MIN LLONG_MIN
#define FT_ULLONG_MAX ULLONG_MAX
#define ft_memchr memchr
#define ft_memcmp memcmp
#define ft_memcpy memcpy
#define ft_memmove memmove
#define ft_memset memset
#define ft_strcmp strcmp
#define ft_strlen strlen
#define ft_strncmp strncmp
#define ft_strncpy strncpy
#define ft_strrchr strrchr
#define ft_scalloc calloc
#define ft_sfree free
#define ft_smalloc malloc
#define ft_srealloc realloc
#define ft_getenv(n) ((char *)0)
/* Clang's private five-word nonlocal-jump buffer, only inside this build.
 * It is deliberately not exported as a public setjmp/libc ABI. */
typedef void *reist_font_jump[5];
#define ft_jmp_buf reist_font_jump
#define ft_setjmp(b) __builtin_setjmp((void **)(b))
#define ft_longjmp(b,v) __builtin_longjmp((void **)(b),1)
char *reist_font_strcat(char *,const char *);
char *reist_font_strstr(const char *,const char *);
#define ft_strcat reist_font_strcat
#define ft_strstr reist_font_strstr
void reist_font_sort(void *,size_t,size_t,int (*)(const void *,const void *));
#define ft_qsort reist_font_sort
#endif
