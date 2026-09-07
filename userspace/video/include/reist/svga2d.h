/**
 * @file reist/svga2d.h
 * @brief Versioned IPC contract for the supervised VMware SVGA-II driver.
 *
 * Coordinates and dimensions follow the VMware SVGA-II FIFO command layout.
 * Ring 3 never receives raw FIFO, port or framebuffer mappings through this
 * protocol; the kernel validates and emits the fixed command forms.
 */
#ifndef REIST_VIDEO_SVGA2D_H
#define REIST_VIDEO_SVGA2D_H

#include <stdint.h>

#define REIST_SVGA2D_ABI_VERSION 1U
#define REIST_SVGA2D_FLAG_RESPONSE (1U << 31U)
#define REIST_SVGA2D_CAP_RECT_FILL (1U << 0U)
#define REIST_SVGA2D_CAP_RECT_COPY (1U << 1U)

enum {
    REIST_SVGA2D_ACTIVATE = 1U,
    REIST_SVGA2D_DEACTIVATE = 2U,
    REIST_SVGA2D_RECT_FILL = 3U,
    REIST_SVGA2D_RECT_COPY = 4U,
    REIST_SVGA2D_INFO = 5U,
    REIST_SVGA2D_ACTIVATE_MODE = 6U,
};

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t operation;
    uint32_t flags;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
    uint32_t color;
    uint32_t capabilities;
    int32_t status;
    uint32_t reserved[2];
} reist_svga2d_message_t;

_Static_assert(sizeof(reist_svga2d_message_t) == 64U,
               "SVGA2D IPC ABI changed");

#endif
