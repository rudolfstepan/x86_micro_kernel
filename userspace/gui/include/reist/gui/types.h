/**
 * @file reist/gui/types.h
 * @brief Common value types shared by public REIST GUI components.
 *
 * Types in this header contain no pointers, authority or hidden allocation.
 * Coordinates are local to the caller-owned surface. Rectangles are half-open:
 * `[x, x + width)` by `[y, y + height)`.
 */
#ifndef REIST_GUI_TYPES_H
#define REIST_GUI_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A half-open rectangle in caller-local surface coordinates. */
typedef struct reist_gui_rect {
    int32_t x;       /**< Left edge in pixels. */
    int32_t y;       /**< Top edge in pixels. */
    uint32_t width;  /**< Width in pixels; zero means empty. */
    uint32_t height; /**< Height in pixels; zero means empty. */
} reist_gui_rect_t;

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_TYPES_H */
