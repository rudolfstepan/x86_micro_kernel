/* ISO C11 byte/string subset. Arguments retain their ordinary C contracts;
 * these are not validators for foreign addresses or unterminated network data. */
#include <string.h>
#include <stdint.h>
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
