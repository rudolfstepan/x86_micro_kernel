/* ISO C11 byte/string subset. Arguments retain their ordinary C contracts;
 * these are not validators for foreign addresses or unterminated network data. */
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

int tolower(int value) { return value >= 'A' && value <= 'Z' ? value + ('a'-'A') : value; }
int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i=0; i<n; ++i) {
        int difference=tolower((unsigned char)a[i])-tolower((unsigned char)b[i]);
        if (difference || !a[i]) return difference;
    }
    return 0;
}
char *strncpy(char *destination, const char *source, size_t n) {
    size_t i=0;
    for (; i<n && source[i]; ++i) destination[i]=source[i];
    for (; i<n; ++i) destination[i]=0;
    return destination;
}
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    if (!count) return NULL;
    /* An unrepresentable array cannot be an ordinary valid C object. */
    if (!size || count > SIZE_MAX/size) return NULL;
    const unsigned char *bytes=base;
    while (count) {
        size_t middle=count/2;
        const void *item=bytes+middle*size;
        int order=compare(key,item);
        if (!order) return (void *)item;
        if (order<0) count=middle;
        else { bytes+=(middle+1)*size; count-=middle+1; }
    }
    return NULL;
}
void *memcpy(void *d,const void *s,size_t n) {
    unsigned char *out=d; const unsigned char *in=s;
    for (size_t i=0;i<n;++i) out[i]=in[i]; return d;
}
void *memmove(void *d,const void *s,size_t n) {
    unsigned char *out=d; const unsigned char *in=s;
    if ((uintptr_t)d<(uintptr_t)s) for (size_t i=0;i<n;++i) out[i]=in[i];
    else while (n) { --n; out[n]=in[n]; }
    return d;
}
void *memset(void *d,int value,size_t n) {
    unsigned char *out=d; for (size_t i=0;i<n;++i) out[i]=(unsigned char)value; return d;
}
int memcmp(const void *a,const void *b,size_t n) {
    const unsigned char *x=a,*y=b;
    for (size_t i=0;i<n;++i) if (x[i]!=y[i]) return (int)x[i]-(int)y[i]; return 0;
}
void *memchr(const void *a,int value,size_t n) {
    const unsigned char *x=a;
    for (size_t i=0;i<n;++i) if (x[i]==(unsigned char)value) return (void *)(x+i); return NULL;
}
size_t strlen(const char *s) { size_t n=0; while (s[n]) ++n; return n; }
int strcmp(const char *a,const char *b) {
    while (*a && *a==*b) { ++a; ++b; } return (unsigned char)*a-(unsigned char)*b;
}
int strncmp(const char *a,const char *b,size_t n) {
    for (size_t i=0;i<n;++i) {
        int diff=(unsigned char)a[i]-(unsigned char)b[i];
        if (diff || !a[i]) return diff;
    } return 0;
}
char *strchr(const char *s,int value) {
    do { if (*s==(char)value) return (char *)s; } while (*s++); return NULL;
}
char *strrchr(const char *s,int value) {
    const char *last=NULL;
    do { if (*s==(char)value) last=s; } while (*s++); return (char *)last;
}
