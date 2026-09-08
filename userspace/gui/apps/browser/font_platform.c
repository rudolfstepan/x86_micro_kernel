#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/internal/ftstream.h>
/* No font path/file authority in the runtime library. Only memory faces. */
FT_BASE_DEF(FT_Error) FT_Stream_Open(FT_Stream stream,const char *path) {
    (void)stream;(void)path;return FT_Err_Cannot_Open_Resource;
}
char *reist_font_strcat(char *out,const char *in) {
    char *p=out+strlen(out);while((*p++=*in++)) {} return out;
}
char *reist_font_strstr(const char *text,const char *needle) {
    size_t a=strlen(text),b=strlen(needle);if(b>a)return NULL;
    for(size_t i=0;i<=a-b;++i)if(!memcmp(text+i,needle,b))return (char *)text+i;
    return NULL;
}
/* Allocation-free heapsort for the private upstream adapter. */
static void swap(unsigned char *a,unsigned char *b,size_t width) {
    for(size_t i=0;i<width;++i) { unsigned char v=a[i];a[i]=b[i];b[i]=v; }
}
static void sift(unsigned char *base,size_t count,size_t at,size_t size,int (*cmp)(const void *,const void *)) {
    while(at<count/2) {
        size_t child=at*2+1;
        if(child+1<count && cmp(base+child*size,base+(child+1)*size)<0) ++child;
        if(cmp(base+at*size,base+child*size)>=0) break;
        swap(base+at*size,base+child*size,size);at=child;
    }
}
void reist_font_sort(void *pointer,size_t count,size_t size,int (*compare)(const void *,const void *)) {
    if(!size || count<2) return;
    unsigned char *base=pointer;
    for(size_t i=count/2;i;--i) sift(base,count,i-1,size,compare);
    for(size_t i=count-1;i;--i) { swap(base,base+i*size,size);sift(base,i,0,size,compare); }
}
