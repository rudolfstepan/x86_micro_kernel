#ifndef REIST_GUI_HTML_DOCUMENT_H
#define REIST_GUI_HTML_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#define REIST_HTML_INPUT_CAPACITY 65536U
#define REIST_HTML_TEXT_CAPACITY 65536U
#define REIST_HTML_ELEMENT_CAPACITY 512U
#define REIST_HTML_LINK_CAPACITY 128U
#define REIST_HTML_HREF_CAPACITY 256U
#define REIST_HTML_TITLE_CAPACITY 128U
#define REIST_HTML_NESTING_CAPACITY 32U
#define REIST_HTML_IMAGE_CAPACITY 16U
#define REIST_HTML_ANCHOR_CAPACITY 128U

typedef enum reist_html_element_kind {
    REIST_HTML_ELEMENT_TEXT = 1,
    REIST_HTML_ELEMENT_LINE_BREAK,
    REIST_HTML_ELEMENT_PARAGRAPH_BREAK,
    REIST_HTML_ELEMENT_LIST_MARKER,
    REIST_HTML_ELEMENT_IMAGE,
    REIST_HTML_ELEMENT_ANCHOR
} reist_html_element_kind_t;

enum {
    REIST_HTML_STYLE_BOLD = 1U << 0,
    REIST_HTML_STYLE_ITALIC = 1U << 1,
    REIST_HTML_STYLE_PREFORMATTED = 1U << 2,
    REIST_HTML_STYLE_HEADING_1 = 1U << 3,
    REIST_HTML_STYLE_HEADING_2 = 1U << 4,
    REIST_HTML_STYLE_HEADING_3 = 1U << 5,
    REIST_HTML_STYLE_LINK = 1U << 6
};

typedef struct reist_html_element {
    uint32_t kind;
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t style;
    uint32_t link_index;
    uint16_t list_depth;
    uint16_t reserved;
} reist_html_element_t;

typedef struct reist_html_link {
    char href[REIST_HTML_HREF_CAPACITY];
} reist_html_link_t;

typedef struct reist_html_image {
    char source[REIST_HTML_HREF_CAPACITY];
    char alt[128U];
    uint16_t width;
    uint16_t height;
} reist_html_image_t;

typedef struct reist_html_anchor {
    char name[128U];
} reist_html_anchor_t;

typedef struct reist_html_document {
    char title[REIST_HTML_TITLE_CAPACITY];
    char text[REIST_HTML_TEXT_CAPACITY];
    reist_html_element_t elements[REIST_HTML_ELEMENT_CAPACITY];
    reist_html_link_t links[REIST_HTML_LINK_CAPACITY];
    uint32_t text_length;
    uint32_t element_count;
    uint32_t link_count;
    reist_html_image_t images[REIST_HTML_IMAGE_CAPACITY];
    reist_html_anchor_t anchors[REIST_HTML_ANCHOR_CAPACITY];
    uint32_t image_count;
    uint32_t anchor_count;
} reist_html_document_t;

enum {
    REIST_HTML_OK = 0,
    REIST_HTML_INVALID = -22,
    REIST_HTML_CAPACITY = -28,
    REIST_HTML_ENCODING = -84
};

int reist_html_document_parse(const uint8_t *input, size_t length,
                              reist_html_document_t *document);

/** Resolve the browser subset of absolute, root-relative, document-relative
 * and fragment references without allocating.  Only local absolute paths and
 * HTTP(S) network URLs are publishable navigation targets. */
int reist_html_url_resolve(const char *base, const char *reference,
                           char *output, size_t capacity);
/* Append-only wide resolver adapter. Legacy document fields and resolver
 * admission stay 256 bytes. Scratch is private, caller-owned and not shared
 * between simultaneous calls; no allocation or implicit global storage. */
#define REIST_HTML_URL_CAPACITY 8193U
typedef struct reist_html_url_workspace {
    char candidate[REIST_HTML_URL_CAPACITY], path[REIST_HTML_URL_CAPACITY];
    uint16_t marks[REIST_HTML_URL_CAPACITY];
} reist_html_url_workspace_t;
int reist_html_url_resolve_wide(const char *base,const char *reference,
    char *output,size_t capacity,reist_html_url_workspace_t *workspace);

/** Normalize direct address-bar input. Explicit HTTP(S) and local absolute
 * paths are retained; a host/path without a scheme receives https://. */
int reist_html_navigation_normalize(const char *input, char *output,
                                    size_t capacity);

#endif
