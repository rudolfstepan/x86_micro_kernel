#ifndef BROWSER_SCENE_H
#define BROWSER_SCENE_H
#include "browser_model.h"
#include "reist/gui/font.h"
#include "html_protocol.h"
#include "browser_resources.h"
#include "browser_forms.h"
#include "font_engine.hpp"
#define BROWSER_CSS_RESOURCE_VERSION 2U
#define BROWSER_CSS_DOCUMENT_VERSION 4U
#define BROWSER_CSS_SCRIPT_VERSION 5U
#define BROWSER_CSS_INPUT_BYTES (BROWSER_DOCUMENT_INPUT_CAPACITY+BROWSER_RESOURCE_WIRE_CAPACITY)
#define BROWSER_CSS_FONT_MAX 64U
#define BROWSER_SCENE_VERSION 3U
#define BROWSER_SCENE_DOCUMENT_VERSION 4U
#define BROWSER_SCENE_LAYOUT_VERSION 5U
#define BROWSER_SCENE_FONT_VERSION 6U
#define BROWSER_SCENE_ROUND 9U
#define BROWSER_SCENE_SHADOW 10U
#define BROWSER_SCENE_NO_UNDERLINE 128U
#define BROWSER_SCENE_RUNS 2048U
#define BROWSER_SCENE_COORD_LIMIT 262144
#define BROWSER_SCENE_FILL 7U
#define BROWSER_SCENE_CONTROL 8U
typedef struct browser_scene_run {
    uint32_t kind, offset, length, link;
    int32_t x, y;
    uint32_t width, height, color, flags;
} browser_scene_run_t;
typedef struct browser_scene {
    uint32_t version, width, height, total_height, count;
    browser_scene_run_t runs[BROWSER_SCENE_RUNS];
    browser_forms_t forms;
    char image_urls[REIST_HTML_IMAGE_CAPACITY][BROWSER_RESOURCE_URL_CAPACITY];
    browser_font_atlas fonts;
} browser_scene_t;
/* Private CSS1 envelope over generation-scoped bulk IPC, not a Surface ABI.
 * Input is this fixed prefix followed by exactly header.input_length bytes. */
typedef struct browser_css_request {
    browser_html_header_t header;
    uint32_t version, width, height, image_sizes[16][2];
    char document_url[256];
} browser_css_request_t;
#define BROWSER_CSS_SCENE_WIRE_CAPACITY (8U+sizeof(browser_html_reply_t)+sizeof(browser_scene_t))
#define BROWSER_CSS_WIRE_CAPACITY (BROWSER_CSS_SCENE_WIRE_CAPACITY>sizeof(browser_resource_needs_t) ? BROWSER_CSS_SCENE_WIRE_CAPACITY : sizeof(browser_resource_needs_t))
#define BROWSER_CSS_PACKET_MAGIC 0x31535343U
#define BROWSER_CSS_PACKET_DATA 2032U
typedef struct browser_css_packet {
    uint32_t magic, request, offset, total;
    uint8_t bytes[BROWSER_CSS_PACKET_DATA];
} browser_css_packet_t;
#ifdef __cplusplus
extern "C" {
#endif
int browser_css_request_validate(const browser_css_request_t *);
int browser_css_pack(const browser_html_reply_t *,const browser_scene_t *,uint8_t *,size_t);
int browser_css_unpack(const uint8_t *,size_t,const browser_css_request_t *,uint32_t,uint32_t,
    browser_html_reply_t *,browser_scene_t *);
/* Receive admission is side-effect-free on failure, including offset/total. */
int browser_css_packet_accept(const browser_css_packet_t *,uint32_t packet_length,
    uint32_t request,uint8_t *,uint32_t capacity,uint32_t *offset,uint32_t *total);
int browser_scene_validate(const reist_html_document_t *, const browser_scene_t *);
/* Pixel output is private until successful return; callers must not publish
 * partial output. All clipping and draw-work admission precede publication.
 * Single-threaded private renderer: fixed glyph scratch is invalidated for
 * each call, and does not retain font/resource authority between frames. */
int browser_scene_raster(const reist_html_document_t *, const browser_scene_t *,
    const reist_gui_font_t *, const browser_image_slot_t *, uint32_t scroll,
    uint32_t *pixels, uint32_t width, uint32_t height, uint32_t top, uint32_t view);
int browser_scene_raster_forms(const reist_html_document_t *, const browser_scene_t *,
    const reist_gui_font_t *, const browser_image_slot_t *, const browser_form_state_t *, uint32_t scroll,
    uint32_t *pixels, uint32_t width, uint32_t height, uint32_t top, uint32_t view);
#ifdef __cplusplus
}
#endif
#endif
