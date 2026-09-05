#ifndef REIST_BROWSER_HTML_ENGINE_H
#define REIST_BROWSER_HTML_ENGINE_H
#include "reist/gui/html_document.h"
/* Worker-local HTML5 tree and semantic projection. Not a public DOM API.
 * Single call per process generation; no network, file or device authority.
 * Input 64 KiB, arena 4 MiB, 2048 cumulative nodes, 4096 attributes, string
 * pool 256 KiB, depth 128, callback work 262144. Errors publish no result. */
int browser_html5_parse(const uint8_t *input, size_t length,
                        reist_html_document_t *document);
#endif
