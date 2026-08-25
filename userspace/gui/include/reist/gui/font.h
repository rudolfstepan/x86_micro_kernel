/**
 * @file reist/gui/font.h
 * @brief Bounded caller-owned PSF version 2 bitmap font view.
 */
#ifndef REIST_GUI_FONT_H
#define REIST_GUI_FONT_H

#include <stddef.h>
#include <stdint.h>

#define REIST_GUI_FONT_API_VERSION 1U
#define REIST_GUI_FONT_PSF2_MAGIC 0x864AB572U
#define REIST_GUI_FONT_PSF2_HAS_UNICODE_TABLE 0x00000001U
#define REIST_GUI_FONT_MAX_FILE_BYTES (4U * 1024U * 1024U)
#define REIST_GUI_FONT_MAX_GLYPHS 131072U
#define REIST_GUI_FONT_MAX_WIDTH 32U
#define REIST_GUI_FONT_MAX_HEIGHT 32U
#define REIST_GUI_FONT_EMPTY_GLYPH UINT32_MAX

typedef struct {
    uint32_t scalar;
    uint32_t glyph_index;
} reist_gui_font_mapping_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    const uint8_t *data;
    size_t data_size;
    size_t glyph_data_offset;
    size_t unicode_data_offset;
    uint32_t glyph_count;
    uint32_t bytes_per_glyph;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t fallback_glyph;
    reist_gui_font_mapping_t *mappings;
    uint32_t mapping_capacity;
    uint32_t mapping_count;
} reist_gui_font_t;

int reist_gui_font_open_psf2(
    reist_gui_font_t *font, const uint8_t *data, size_t data_size,
    reist_gui_font_mapping_t *mappings, uint32_t mapping_capacity,
    uint32_t fallback_scalar);

/** Returns 1 for an exact scalar mapping, 0 for fallback, or a negative error. */
int reist_gui_font_lookup(const reist_gui_font_t *font, uint32_t scalar,
                          uint32_t *glyph_index);

int reist_gui_font_raster_xrgb(
    const reist_gui_font_t *font, uint32_t glyph_index,
    uint32_t foreground_rgb, uint32_t background_rgb,
    uint32_t *pixels, uint32_t stride_pixels, size_t pixel_capacity);

int reist_gui_font_raster_xrgb_region(
    const reist_gui_font_t *font, uint32_t glyph_index,
    uint32_t source_x, uint32_t source_y, uint32_t width, uint32_t height,
    uint32_t foreground_rgb, uint32_t background_rgb,
    uint32_t *pixels, uint32_t stride_pixels, size_t pixel_capacity);

#endif
