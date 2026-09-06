#ifndef BROWSER_HTML_PROTOCOL_H
#define BROWSER_HTML_PROTOCOL_H
#include "reist/gui/html_document.h"
/* Private little-endian i386 file adapter, not a public DOM ABI. No pointers.
 * The parent owns both paths and reaps the only writer before reading/cleanup. */
#define BROWSER_HTML_MAGIC 0x354C5448U
#define BROWSER_HTML_VERSION 2U
#define BROWSER_HTML_DOCUMENT_VERSION 3U
#define BROWSER_DOCUMENT_INPUT_CAPACITY (1024U*1024U)
enum { BROWSER_ENCODING_AUTO, BROWSER_ENCODING_UTF8, BROWSER_ENCODING_WINDOWS1252,
       BROWSER_ENCODING_UTF16LE, BROWSER_ENCODING_UTF16BE };
/* WHATWG labels for the supported decoder subset. Unknown labels fail closed. */
static inline uint32_t browser_encoding_label(const char *s,size_t n) {
    static const char *const labels[]={"utf-8","utf8","unicode-1-1-utf-8",
        "windows-1252","cp1252","x-cp1252","iso-8859-1","iso8859-1","iso88591",
        "iso_8859-1","iso_8859-1:1987","latin1","l1","ibm819","cp819","csisolatin1",
        "ascii","us-ascii","ansi_x3.4-1968","utf-16","utf-16le","utf-16be"};
    if(!s) return UINT32_MAX;
    while(n && (*s==' ' || *s=='\t' || *s=='\n' || *s=='\r' || *s=='\f')) { ++s; --n; }
    while(n && (s[n-1]==' ' || s[n-1]=='\t' || s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]=='\f')) --n;
    for(uint32_t i=0;i<sizeof(labels)/sizeof(labels[0]);++i) {
        size_t j=0; for(;j<n && labels[i][j];++j) {
            char c=s[j]; if(c>='A' && c<='Z') c+='a'-'A';
            if(c!=labels[i][j]) break;
        }
        if(j==n && !labels[i][j]) return i<3 ? BROWSER_ENCODING_UTF8 : i<19 ?
            BROWSER_ENCODING_WINDOWS1252 : i<21 ? BROWSER_ENCODING_UTF16LE : BROWSER_ENCODING_UTF16BE;
    }
    return UINT32_MAX;
}
#define BROWSER_HTML_DEADLINE_MS 5000U
typedef struct browser_html_header {
    uint32_t magic, version, size, request, parent_pid, parent_generation;
    uint32_t child_pid, child_generation, input_length, mode, reserved[2];
} browser_html_header_t;
static inline int browser_html_profile_valid(const browser_html_header_t *h) {
    return h && !h->reserved[1] && (h->version==BROWSER_HTML_VERSION ?
        !h->reserved[0] && h->input_length<=REIST_HTML_INPUT_CAPACITY :
        h->version==BROWSER_HTML_DOCUMENT_VERSION && h->input_length<=BROWSER_DOCUMENT_INPUT_CAPACITY &&
        h->reserved[0]<=BROWSER_ENCODING_UTF16BE);
}
typedef struct browser_html_reply {
    browser_html_header_t header;
    reist_html_document_t document;
} browser_html_reply_t;
int browser_html_validate(const browser_html_reply_t *reply, size_t length,
                          const browser_html_header_t *request,
                          uint32_t child_pid, uint32_t child_generation);
int browser_html_document_validate(const reist_html_document_t *document);
/* V2 wire: header, title[128], five counts (text/elements/links/images/anchors),
 * then the occupied prefix of each array in that order. Header.size is wire
 * length; unpack checks it and normalizes size to the in-memory reply size.
 * This private revision does not accept the unshipped padded V1 wire format. */
int browser_html_pack(const browser_html_reply_t *reply, uint8_t *wire, size_t capacity);
int browser_html_unpack(const uint8_t *wire, size_t length, browser_html_reply_t *reply);
#endif
