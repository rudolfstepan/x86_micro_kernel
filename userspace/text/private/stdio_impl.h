#ifndef REIST_TEXT_STREAM_H
#define REIST_TEXT_STREAM_H
#include <stdio.h>
#include <stdint.h>
#define EOF (-1)
#define NL_ARGMAX 9
#define F_ERR 32
typedef struct reist_text_stream {
    unsigned flags;
    int lbf,lock;
    unsigned char *buf,*wpos,*wbase,*wend;
    size_t buf_size,remaining;
    void *cookie;
    size_t (*write)(struct reist_text_stream *,const unsigned char *,size_t);
} reist_text_stream;
/* All instances are stack-owned, internal, single-caller memory destinations.
 * These names do not publish a FILE API or emulate OS file/lock operations. */
#define FILE reist_text_stream
#define vfprintf reist_text_vformat
#define FLOCK(f) ((void)(f))
#define FUNLOCK(f) ((void)(f))
#define ferror(f) ((f)->flags & F_ERR)
#define __fwritex reist_text_write
#define __towrite reist_text_begin
#define strnlen reist_text_strnlen
#define strerror reist_text_error
#define isdigit reist_text_isdigit
static inline int reist_text_isdigit(int c) { return (unsigned)(c-'0')<10U; }
int reist_text_vformat(reist_text_stream *,const char *,va_list);
int reist_text_begin(reist_text_stream *);
size_t reist_text_write(const void *,size_t,reist_text_stream *);
void reist_text_repeat(reist_text_stream *,char,size_t);
size_t reist_text_strnlen(const char *,size_t);
const char *reist_text_error(int);
#endif
