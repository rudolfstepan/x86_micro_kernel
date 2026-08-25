#ifndef REIST_DISPLAY_CONTROL_H
#define REIST_DISPLAY_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_CONTROL_ABI_VERSION 1U
#define DISPLAY_CONTROL_ACTIVATE 1U
#define DISPLAY_CONTROL_DEACTIVATE 2U
#define DISPLAY_CONTROL_FRAME_BEGIN 3U
#define DISPLAY_CONTROL_FRAME_COMMIT 4U
#define DISPLAY_CONTROL_FRAME_CANCEL 5U
#define DISPLAY_CONTROL_FRAME_STAGE_BLIT 6U
#define DISPLAY_CONTROL_DRAW_PIXELS 7U
#define DISPLAY_CONTROL_SURFACE_BUFFER_CREATE 8U
#define DISPLAY_CONTROL_SURFACE_BUFFER_DESTROY 9U
#define DISPLAY_CONTROL_SURFACE_BUFFER_DRAW 10U
#define DISPLAY_CONTROL_DRIVER_COMMAND 11U
#define DISPLAY_CONTROL_FRAME_MARK_ACCELERATED 12U
#define DISPLAY_CONTROL_PRESENT_CAPACITY 8U

#define DISPLAY_DRIVER_ACTIVATE 1U
#define DISPLAY_DRIVER_DEACTIVATE 2U
#define DISPLAY_DRIVER_RECT_FILL 3U
#define DISPLAY_DRIVER_RECT_COPY 4U
#define DISPLAY_DRIVER_BUSY_QUERY 5U
/** Passive, read-only hardware identity snapshot; never enables an engine. */
#define DISPLAY_DRIVER_PROBE 6U
#define DISPLAY_DRIVER_CAP_RECT_FILL (1U << 0U)
#define DISPLAY_DRIVER_CAP_RECT_COPY (1U << 1U)

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

/* Append-only syscall-109 payload for one packed XRGB8888 image upload. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixels_address;
    uint32_t pixel_count;
    uint32_t reserved;
} display_pixels_request_t;

/* Immutable, parent-consumer-bound pixel resource used by the Ring-3
 * compositor.  CREATE copies and validates the complete client buffer before
 * publishing its generation. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t buffer_id;
    uint32_t buffer_generation;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixels_address;
    uint32_t pixel_count;
    uint32_t reserved;
} display_surface_buffer_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t buffer_id;
    uint32_t buffer_generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t source_x;
    uint32_t source_y;
    int32_t destination_x;
    int32_t destination_y;
    uint32_t width;
    uint32_t height;
    uint32_t reserved[2];
} display_surface_buffer_draw_request_t;

/* Append-only syscall-109 payload used only by a supervised display driver.
 * The device handle binds every request to one live process generation. */
typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t device;
    uint32_t command;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
    uint32_t capabilities;
    uint32_t busy;
    int32_t status;
    uint32_t reserved[4];
} display_driver_request_t;

_Static_assert(sizeof(display_driver_request_t) == 80U,
               "display driver request ABI changed");

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
bool display_control_graphics_active(void);
bool display_control_vmware_acceleration_active(void);
int display_control_driver_command(display_driver_request_t *request);

#endif
