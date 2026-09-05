#ifndef BROWSER_HTML_PROTOCOL_H
#define BROWSER_HTML_PROTOCOL_H
#include "reist/gui/html_document.h"
/* Private little-endian i386 file adapter, not a public DOM ABI. No pointers.
 * The parent owns both paths and reaps the only writer before reading/cleanup. */
#define BROWSER_HTML_MAGIC 0x354C5448U
#define BROWSER_HTML_VERSION 2U
#define BROWSER_HTML_DEADLINE_MS 5000U
typedef struct browser_html_header {
    uint32_t magic, version, size, request, parent_pid, parent_generation;
    uint32_t child_pid, child_generation, input_length, mode, reserved[2];
} browser_html_header_t;
typedef struct browser_html_reply {
    browser_html_header_t header;
    reist_html_document_t document;
} browser_html_reply_t;
int browser_html_validate(const browser_html_reply_t *reply, size_t length,
                          const browser_html_header_t *request,
                          uint32_t child_pid, uint32_t child_generation);
/* V2 wire: header, title[128], five counts (text/elements/links/images/anchors),
 * then the occupied prefix of each array in that order. Header.size is wire
 * length; unpack checks it and normalizes size to the in-memory reply size.
 * This private revision does not accept the unshipped padded V1 wire format. */
int browser_html_pack(const browser_html_reply_t *reply, uint8_t *wire, size_t capacity);
int browser_html_unpack(const uint8_t *wire, size_t length, browser_html_reply_t *reply);
#endif
