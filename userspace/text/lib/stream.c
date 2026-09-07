/* Allocation-free private memory stream, not a public FILE implementation. */
#include "stdio_impl.h"
#include <string.h>
#include <errno.h>
#include <wchar.h>

int reist_text_begin(reist_text_stream *f) {
    f->wbase=f->wpos=f->buf; f->wend=f->buf+f->buf_size; return 0;
}
size_t reist_text_write(const void *data,size_t size,reist_text_stream *f) {
    size_t kept=size<f->remaining?size:f->remaining;
    f->remaining-=kept;
    /* sn_write owns termination and its destination cursor, even when full. */
    return f->write(f,data,size);
}
void reist_text_repeat(reist_text_stream *f,char value,size_t count) {
    unsigned char block[256];
    if(count>f->remaining) count=f->remaining;
    if(!count) return;
    memset(block,(unsigned char)value,count<sizeof block?count:sizeof block);
    while(count) {
        size_t n=count<sizeof block?count:sizeof block;
        reist_text_write(block,n,f); count-=n;
    }
}
size_t reist_text_strnlen(const char *text,size_t limit) {
    size_t n=0; while(n<limit && text[n]) ++n; return n;
}
int reist_text_wctomb(char *out,wchar_t value) {
    /* C-locale basic characters only; no state or locale mutation. */
    if(!out) return 0;
    if((uint32_t)value>127U) { errno=EILSEQ; return -1; }
    out[0]=(char)value; return 1;
}
const char *reist_text_error(int code) {
    /* Private musl %m extension; no public strerror/locale promise. */
    switch(code) {
    case 0: return "No error information";
    case ENOMEM: return "Out of memory";
    case EBUSY: return "Resource busy";
    case EINVAL: return "Invalid argument";
    case EOVERFLOW: return "Value too large for data type";
    case EILSEQ: return "Illegal byte sequence";
    default: return "Unknown error";
    }
}
