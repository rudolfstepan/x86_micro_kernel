#ifndef BROWSER_CSS_ENGINE_H
#define BROWSER_CSS_ENGINE_H
#include "browser_scene.h"
/* Single generation, real Hubbub tree + LibCSS cascade, fixed worker arena.
 * Intrinsic image dimensions are scalar hints, never pixels or capabilities. */
int browser_css_render(const uint8_t *html, size_t length, uint32_t width,
    uint32_t height, const uint32_t image_sizes[16][2], const char *document_url,
    reist_html_document_t *document, browser_scene_t *scene);
#endif
