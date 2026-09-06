#ifndef BROWSER_CSS_ENGINE_H
#define BROWSER_CSS_ENGINE_H
#include "browser_scene.h"
#include "browser_resources.h"
/* Single generation, real Hubbub tree + LibCSS cascade, fixed worker arena.
 * Intrinsic image dimensions are scalar hints, never pixels or capabilities. */
int browser_css_render(const uint8_t *html, size_t length, uint32_t width,
    uint32_t height, const uint32_t image_sizes[16][2], const char *document_url,
    reist_html_document_t *document, browser_scene_t *scene);
/* 1 requests missing bytes in needs; 0 is a complete scene; negative fails.
 * Discovery also occurs in an isolated disposable worker, never in chrome. */
int browser_css_render_resources(const uint8_t *,size_t,uint32_t,uint32_t,
    const uint32_t [16][2],const char *,const browser_resources_t *,
    browser_resource_needs_t *,reist_html_document_t *,browser_scene_t *);
int browser_css_render_document(const uint8_t *,size_t,uint32_t,uint32_t,
    const uint32_t [16][2],const char *,const browser_resources_t *,
    browser_resource_needs_t *,reist_html_document_t *,browser_scene_t *,uint32_t encoding);
#endif
