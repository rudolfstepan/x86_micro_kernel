#include "html_protocol.h"
_Static_assert(sizeof(browser_html_header_t)==48U,"HTML adapter header ABI");
_Static_assert(sizeof(browser_html_reply_t)==133380U,"HTML adapter reply ABI");
static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *d=destination; const uint8_t *s=source;
    for (size_t i=0; i<length; ++i) d[i]=s[i];
}
static int counts_valid(const uint32_t *c) {
    return c[0]<=REIST_HTML_TEXT_CAPACITY && c[1]<=REIST_HTML_ELEMENT_CAPACITY &&
        c[2]<=REIST_HTML_LINK_CAPACITY && c[3]<=REIST_HTML_IMAGE_CAPACITY && c[4]<=REIST_HTML_ANCHOR_CAPACITY;
}
static size_t packed_size(const uint32_t *c) {
    /* Call only after validating counts: all products fit the fixed reply. */
    return sizeof(browser_html_header_t)+REIST_HTML_TITLE_CAPACITY+5U*sizeof(uint32_t)+c[0]+
        c[1]*sizeof(reist_html_element_t)+c[2]*sizeof(reist_html_link_t)+
        c[3]*sizeof(reist_html_image_t)+c[4]*sizeof(reist_html_anchor_t);
}
int browser_html_pack(const browser_html_reply_t *r, uint8_t *wire, size_t capacity) {
    if (!r || !wire || browser_html_validate(r,sizeof(*r),&r->header,
        r->header.child_pid,r->header.child_generation)!=0) return -84;
    const reist_html_document_t *d=&r->document;
    uint32_t counts[]={d->text_length,d->element_count,d->link_count,d->image_count,d->anchor_count};
    size_t n=packed_size(counts); if (n>capacity) return -28;
    browser_html_header_t header=r->header; header.size=(uint32_t)n;
    size_t at=0;
    const void *parts[]={&header,d->title,counts,d->text,d->elements,d->links,d->images,d->anchors};
    size_t sizes[]={sizeof(header),sizeof(d->title),sizeof(counts),counts[0],
        counts[1]*sizeof(d->elements[0]),counts[2]*sizeof(d->links[0]),
        counts[3]*sizeof(d->images[0]),counts[4]*sizeof(d->anchors[0])};
    for (unsigned i=0; i<8; ++i) { bytes_copy(wire+at,parts[i],sizes[i]); at+=sizes[i]; }
    return (int)n;
}
int browser_html_unpack(const uint8_t *wire, size_t length, browser_html_reply_t *r) {
    const size_t prefix=sizeof(browser_html_header_t)+REIST_HTML_TITLE_CAPACITY+5U*sizeof(uint32_t);
    if (!wire || !r || length<prefix || length>sizeof(*r)) return -84;
    browser_html_header_t header;
    uint32_t counts[5];
    bytes_copy(&header,wire,sizeof(header));
    bytes_copy(counts,wire+sizeof(header)+REIST_HTML_TITLE_CAPACITY,sizeof(counts));
    if (header.magic!=BROWSER_HTML_MAGIC || !browser_html_profile_valid(&header) ||
        header.size!=length || !counts_valid(counts) || packed_size(counts)!=length) return -84;
    /* Only a private candidate is modified; publication follows full semantic
     * and parent/request/generation validation in the caller. */
    for (size_t i=0; i<sizeof(*r); ++i) ((uint8_t *)r)[i]=0;
    reist_html_document_t *d=&r->document;
    r->header=header; r->header.size=sizeof(*r);
    bytes_copy(d->title,wire+sizeof(header),sizeof(d->title));
    d->text_length=counts[0]; d->element_count=counts[1]; d->link_count=counts[2];
    d->image_count=counts[3]; d->anchor_count=counts[4];
    size_t at=prefix;
    void *parts[]={d->text,d->elements,d->links,d->images,d->anchors};
    size_t sizes[]={counts[0],counts[1]*sizeof(d->elements[0]),counts[2]*sizeof(d->links[0]),
        counts[3]*sizeof(d->images[0]),counts[4]*sizeof(d->anchors[0])};
    for (unsigned i=0; i<5; ++i) { bytes_copy(parts[i],wire+at,sizes[i]); at+=sizes[i]; }
    return 0;
}
/* Validate UTF-8 scalars, rejecting overlong encodings and surrogate values. */
static int utf8(const char *s, size_t n) {
    for (size_t i=0; i<n;) {
        uint32_t c=(uint8_t)s[i++], minimum=0, extra=0;
        if (c<0x80U) { if (!c) return 0; continue; }
        if (c>=0xC2U && c<=0xDFU) { c&=31U; extra=1; minimum=0x80U; }
        else if (c>=0xE0U && c<=0xEFU) { c&=15U; extra=2; minimum=0x800U; }
        else if (c>=0xF0U && c<=0xF4U) { c&=7U; extra=3; minimum=0x10000U; }
        else return 0;
        if (extra>n-i) return 0;
        while (extra--) { uint32_t b=(uint8_t)s[i++]; if ((b&0xC0U)!=0x80U) return 0; c=(c<<6)|(b&63U); }
        if (c<minimum || c>0x10FFFFU || (c>=0xD800U && c<=0xDFFFU)) return 0;
    }
    return 1;
}
static int string(const char *s, size_t capacity) {
    size_t n=0; while (n<capacity && s[n]) ++n;
    return n<capacity && utf8(s,n);
}
int browser_html_validate(const browser_html_reply_t *r, size_t length,
                          const browser_html_header_t *q, uint32_t pid, uint32_t generation) {
    if (!r || !q || length!=sizeof(*r)) return -84;
    const browser_html_header_t *h=&r->header;
    if (h->magic!=BROWSER_HTML_MAGIC || !browser_html_profile_valid(h) || h->version!=q->version ||
        h->size!=sizeof(*r) || !h->request || h->request!=q->request ||
        h->parent_pid!=q->parent_pid || !h->parent_generation ||
        h->parent_generation!=q->parent_generation || !pid || h->child_pid!=pid ||
        !h->child_generation || (generation && h->child_generation!=generation) ||
        h->input_length!=q->input_length || h->mode || h->reserved[0]!=q->reserved[0] || h->reserved[1]!=q->reserved[1]) return -84;
    return browser_html_document_validate(&r->document);
}
int browser_html_document_validate(const reist_html_document_t *d) {
    if (!d) return -84;
    if (d->text_length>REIST_HTML_TEXT_CAPACITY || d->element_count>REIST_HTML_ELEMENT_CAPACITY ||
        d->link_count>REIST_HTML_LINK_CAPACITY || d->image_count>REIST_HTML_IMAGE_CAPACITY ||
        d->anchor_count>REIST_HTML_ANCHOR_CAPACITY || !string(d->title,sizeof(d->title)) ||
        !utf8(d->text,d->text_length)) return -84;
    for (uint32_t i=0; i<d->link_count; ++i) if (!string(d->links[i].href,sizeof(d->links[i].href))) return -84;
    for (uint32_t i=0; i<d->image_count; ++i) {
        if (!string(d->images[i].source,sizeof(d->images[i].source)) ||
            !string(d->images[i].alt,sizeof(d->images[i].alt)) ||
            d->images[i].width>1024U || d->images[i].height>1024U) return -84;
    }
    for (uint32_t i=0; i<d->anchor_count; ++i) if (!string(d->anchors[i].name,sizeof(d->anchors[i].name))) return -84;
    for (uint32_t i=0; i<d->element_count; ++i) {
        const reist_html_element_t *e=&d->elements[i];
        if (e->reserved || (e->style&~127U) || e->list_depth>REIST_HTML_NESTING_CAPACITY ||
            (e->link_index!=UINT32_MAX && e->link_index>=d->link_count) ||
            ((e->style&REIST_HTML_STYLE_LINK) && e->link_index==UINT32_MAX)) return -84;
        switch (e->kind) {
        case REIST_HTML_ELEMENT_TEXT:
            if (e->text_offset>d->text_length || e->text_length>d->text_length-e->text_offset ||
                !utf8(d->text+e->text_offset,e->text_length)) return -84;
            break;
        case REIST_HTML_ELEMENT_IMAGE: if (e->text_offset>=d->image_count || e->text_length) return -84; break;
        case REIST_HTML_ELEMENT_ANCHOR: if (e->text_offset>=d->anchor_count || e->text_length) return -84; break;
        case REIST_HTML_ELEMENT_LIST_MARKER: if (e->text_offset || e->text_length>REIST_HTML_INPUT_CAPACITY) return -84; break;
        case REIST_HTML_ELEMENT_LINE_BREAK:
        case REIST_HTML_ELEMENT_PARAGRAPH_BREAK: if (e->text_offset || e->text_length) return -84; break;
        default: return -84;
        }
    }
    return 0;
}
