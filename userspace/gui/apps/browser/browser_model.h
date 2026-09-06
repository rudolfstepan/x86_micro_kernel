#ifndef REIST_BROWSER_MODEL_H
#define REIST_BROWSER_MODEL_H
#include "reist/gui/html_document.h"
#include "reist/gui/value_controls.h"
#define BROWSER_LAYOUT_LINE_CAPACITY 2048U
#define BROWSER_IMAGE_CACHE_COUNT 8U
#define BROWSER_IMAGE_CACHE_SIDE 256U
#define BROWSER_BODY_FONT 16U
#define BROWSER_CONTENT_TOP 76U
#define BROWSER_STATUS_HEIGHT 22U
#define BROWSER_SCROLLBAR_WIDTH 18U

typedef struct browser_image_slot {
    uint32_t decoded, width, height, source_width, source_height;
    uint32_t pixels[BROWSER_IMAGE_CACHE_SIDE * BROWSER_IMAGE_CACHE_SIDE];
} browser_image_slot_t;
typedef struct browser_layout_run {
    uint32_t kind, text_offset, text_length, style, link_index;
    int32_t x, y;
    uint32_t width, height;
} browser_layout_run_t;
typedef struct browser_layout {
    browser_layout_run_t runs[BROWSER_LAYOUT_LINE_CAPACITY];
    uint32_t run_count, total_height;
} browser_layout_t;
typedef struct browser_scrollbar {
    reist_gui_range_model_t model;
    reist_gui_range_state_t state;
    reist_gui_rect_t bounds, track, thumb;
    int32_t grab, origin_y, origin_value;
    uint32_t view;
} browser_scrollbar_t;

uint32_t browser_point_in_rect(reist_gui_rect_t rect, int32_t x, int32_t y);
int browser_build_layout(const reist_html_document_t *document, uint32_t width,
                          const browser_image_slot_t *images, browser_layout_t *layout);
int browser_anchor_y(const reist_html_document_t *, const browser_layout_t *,
                      const char *fragment, uint32_t *y);
int browser_address_edit(char *text, uint32_t capacity, uint32_t *length,
                          uint32_t *cursor, uint32_t *replace, uint32_t key);
void browser_scrollbar_configure(browser_scrollbar_t *, uint32_t width,
                                 uint32_t view, uint32_t total, uint32_t position);
int browser_scrollbar_pointer(browser_scrollbar_t *, uint32_t motion,
                               uint32_t pressed, int32_t x, int32_t y);
#endif
