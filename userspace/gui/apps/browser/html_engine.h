#ifndef REIST_BROWSER_HTML_ENGINE_H
#define REIST_BROWSER_HTML_ENGINE_H
#include "reist/gui/html_document.h"
#include "html_protocol.h"
/* Private worker-local retained tree. Never serialized as pointers. */
typedef struct attribute attribute;
typedef struct node node;
struct attribute { char *name, *value; attribute *next; };
struct node {
    uint32_t kind, ns, refs;
    char *name, *text;
    size_t length;
    node *parent, *first, *last, *next, *previous, *form;
    attribute *attributes;
    void *css_data, *css_style, *css_classes;
    uint32_t form_index, control_index; /* worker-local index + 1; zero absent */
};
int browser_html5_tree(const uint8_t *input, size_t length, node **root);
int browser_html5_tree_with_heap(const uint8_t *input,size_t length,node **root,uint32_t private_heap);
int browser_html5_document_tree(const uint8_t *,size_t,uint32_t encoding,node **root);
void browser_html5_document_release(void);
/* Worker-local HTML5 tree and semantic projection. Not a public DOM API.
 * Single call per process generation; no network, file or device authority.
 * Legacy input 64 KiB, demand-backed heap budget 4 MiB, 2048 cumulative nodes, 4096 attributes, string
 * pool 256 KiB, depth 128, callback work 262144. Errors publish no result. */
int browser_html5_parse(const uint8_t *input, size_t length,
                        reist_html_document_t *document);
#endif
