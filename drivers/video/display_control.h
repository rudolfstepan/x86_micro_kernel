#ifndef REIST_DISPLAY_CONTROL_H
#define REIST_DISPLAY_CONTROL_H

#include <stdint.h>

#define DISPLAY_CONTROL_ABI_VERSION 1U
#define DISPLAY_CONTROL_ACTIVATE 1U
#define DISPLAY_CONTROL_DEACTIVATE 2U
#define DISPLAY_CONTROL_FRAME_BEGIN 3U
#define DISPLAY_CONTROL_FRAME_COMMIT 4U
#define DISPLAY_CONTROL_FRAME_CANCEL 5U
#define DISPLAY_CONTROL_FRAME_STAGE_BLIT 6U
#define DISPLAY_CONTROL_PRESENT_CAPACITY 8U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t reserved;
} display_control_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t serial;
    uint32_t reserved;
} display_frame_request_t;

/* Append-only syscall-109 payload for one transaction-local shadow copy.
 * The kernel validates both rectangles before capturing any source pixel. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t serial;
    uint32_t reserved;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
} display_frame_blit_request_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} display_control_rect_t;

int display_control_activate(void);
int display_control_deactivate(void);
void display_control_prepare(void);
void display_control_present_rect(uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);
void display_control_present_rects(const display_control_rect_t *rects,
                                   uint32_t count);

#endif
