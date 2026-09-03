/** @file userspace/gui/lib/font.c */
#include "reist/gui/font.h"

#include "../../../include/reist/utf.h"

#define PSF2_HEADER_BYTES 32U
#define PSF2_SEPARATOR 0xFFU
#define PSF2_SEQUENCE 0xFEU

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void clear_mappings(reist_gui_font_mapping_t *mappings,
                           uint32_t capacity) {
    if (mappings == NULL) return;
    for (uint32_t index = 0U; index < capacity; ++index) {
        mappings[index].scalar = 0U;
        mappings[index].glyph_index = REIST_GUI_FONT_EMPTY_GLYPH;
    }
}

static uint32_t mapping_start(uint32_t scalar, uint32_t capacity) {
    return (scalar * 2654435761U) % capacity;
}

static int insert_mapping(reist_gui_font_mapping_t *mappings,
                          uint32_t capacity, uint32_t scalar,
                          uint32_t glyph_index) {
    uint32_t slot = mapping_start(scalar, capacity);
    for (uint32_t probe = 0U; probe < capacity; ++probe) {
        reist_gui_font_mapping_t *entry = &mappings[slot];
        if (entry->glyph_index == REIST_GUI_FONT_EMPTY_GLYPH) {
            entry->scalar = scalar;
            entry->glyph_index = glyph_index;
            return 0;
        }
        if (entry->scalar == scalar) return -84;
        if (++slot == capacity) slot = 0U;
    }
    return -28;
}

static int font_valid(const reist_gui_font_t *font) {
    return font != NULL && font->version == REIST_GUI_FONT_API_VERSION &&
        font->struct_size == sizeof(*font) && font->data != NULL &&
        font->data_size <= REIST_GUI_FONT_MAX_FILE_BYTES &&
        font->glyph_count != 0U &&
        font->glyph_count <= REIST_GUI_FONT_MAX_GLYPHS &&
        font->width != 0U && font->width <= REIST_GUI_FONT_MAX_WIDTH &&
        font->height != 0U && font->height <= REIST_GUI_FONT_MAX_HEIGHT &&
        font->row_bytes == (font->width + 7U) / 8U &&
        font->bytes_per_glyph == font->row_bytes * font->height &&
        font->fallback_glyph < font->glyph_count && font->mappings != NULL &&
        font->mapping_capacity != 0U;
}

int reist_gui_font_lookup(const reist_gui_font_t *font, uint32_t scalar,
                          uint32_t *glyph_index) {
    if (!font_valid(font) || glyph_index == NULL || scalar > 0x10FFFFU ||
        (scalar >= 0xD800U && scalar <= 0xDFFFU)) return -22;
    uint32_t slot = mapping_start(scalar, font->mapping_capacity);
    for (uint32_t probe = 0U; probe < font->mapping_capacity; ++probe) {
        const reist_gui_font_mapping_t *entry = &font->mappings[slot];
        if (entry->glyph_index == REIST_GUI_FONT_EMPTY_GLYPH) break;
        if (entry->scalar == scalar) {
            if (entry->glyph_index >= font->glyph_count) return -84;
            *glyph_index = entry->glyph_index;
            return 1;
        }
        if (++slot == font->mapping_capacity) slot = 0U;
    }
    *glyph_index = font->fallback_glyph;
    return 0;
}

int reist_gui_font_open_psf2(
    reist_gui_font_t *font, const uint8_t *data, size_t data_size,
    reist_gui_font_mapping_t *mappings, uint32_t mapping_capacity,
    uint32_t fallback_scalar) {
    if (font == NULL || data == NULL || mappings == NULL ||
        mapping_capacity == 0U || data_size < PSF2_HEADER_BYTES ||
        data_size > REIST_GUI_FONT_MAX_FILE_BYTES) return -22;
    uint32_t magic = read_u32_le(data + 0U);
    uint32_t version = read_u32_le(data + 4U);
    uint32_t header_size = read_u32_le(data + 8U);
    uint32_t flags = read_u32_le(data + 12U);
    uint32_t glyph_count = read_u32_le(data + 16U);
    uint32_t bytes_per_glyph = read_u32_le(data + 20U);
    uint32_t height = read_u32_le(data + 24U);
    uint32_t width = read_u32_le(data + 28U);
    uint32_t row_bytes = width <= REIST_GUI_FONT_MAX_WIDTH
        ? (width + 7U) / 8U : 0U;
    uint64_t glyph_bytes = (uint64_t)glyph_count * bytes_per_glyph;
    if (magic != REIST_GUI_FONT_PSF2_MAGIC || version != 0U ||
        header_size < PSF2_HEADER_BYTES || header_size > data_size ||
        flags != REIST_GUI_FONT_PSF2_HAS_UNICODE_TABLE ||
        glyph_count == 0U || glyph_count > REIST_GUI_FONT_MAX_GLYPHS ||
        width == 0U || width > REIST_GUI_FONT_MAX_WIDTH ||
        height == 0U || height > REIST_GUI_FONT_MAX_HEIGHT ||
        bytes_per_glyph == 0U || bytes_per_glyph != row_bytes * height ||
        glyph_bytes > data_size - header_size) return -84;
    size_t unicode_offset = header_size + (size_t)glyph_bytes;
    size_t cursor = unicode_offset;
    uint32_t mapping_count = 0U;
    for (uint32_t glyph = 0U; glyph < glyph_count; ++glyph) {
        int sequence = 0;
        int terminated = 0;
        while (cursor < data_size) {
            uint8_t marker = data[cursor];
            if (marker == PSF2_SEPARATOR) {
                ++cursor;
                terminated = 1;
                break;
            }
            if (marker == PSF2_SEQUENCE) {
                sequence = 1;
                ++cursor;
                continue;
            }
            size_t consumed = 0U;
            uint32_t scalar = 0U;
            if (!reist_utf8_decode_one((const char *)data + cursor,
                                       data_size - cursor,
                                       &consumed, &scalar)) return -84;
            cursor += consumed;
            if (!sequence && ++mapping_count > mapping_capacity) return -28;
        }
        if (!terminated) return -84;
    }
    if (cursor != data_size) return -84;

    clear_mappings(mappings, mapping_capacity);
    cursor = unicode_offset;
    uint32_t published = 0U;
    for (uint32_t glyph = 0U; glyph < glyph_count; ++glyph) {
        int sequence = 0;
        while (cursor < data_size && data[cursor] != PSF2_SEPARATOR) {
            if (data[cursor] == PSF2_SEQUENCE) {
                sequence = 1;
                ++cursor;
                continue;
            }
            size_t consumed = 0U;
            uint32_t scalar = 0U;
            if (!reist_utf8_decode_one((const char *)data + cursor,
                                       data_size - cursor,
                                       &consumed, &scalar) ||
                (!sequence && insert_mapping(
                    mappings, mapping_capacity, scalar, glyph) != 0)) {
                clear_mappings(mappings, mapping_capacity);
                return -84;
            }
            cursor += consumed;
            if (!sequence) ++published;
        }
        if (cursor >= data_size) {
            clear_mappings(mappings, mapping_capacity);
            return -84;
        }
        ++cursor;
    }
    reist_gui_font_t candidate = {
        .version = REIST_GUI_FONT_API_VERSION,
        .struct_size = sizeof(candidate),
        .data = data,
        .data_size = data_size,
        .glyph_data_offset = header_size,
        .unicode_data_offset = unicode_offset,
        .glyph_count = glyph_count,
        .bytes_per_glyph = bytes_per_glyph,
        .width = width,
        .height = height,
        .row_bytes = row_bytes,
        .fallback_glyph = 0U,
        .mappings = mappings,
        .mapping_capacity = mapping_capacity,
        .mapping_count = published,
    };
    uint32_t fallback = 0U;
    if (reist_gui_font_lookup(&candidate, fallback_scalar, &fallback) != 1) {
        clear_mappings(mappings, mapping_capacity);
        return -84;
    }
    candidate.fallback_glyph = fallback;
    *font = candidate;
    return 0;
}

int reist_gui_font_raster_xrgb(
    const reist_gui_font_t *font, uint32_t glyph_index,
    uint32_t foreground_rgb, uint32_t background_rgb,
    uint32_t *pixels, uint32_t stride_pixels, size_t pixel_capacity) {
    return reist_gui_font_raster_xrgb_region(
        font, glyph_index, 0U, 0U, font != NULL ? font->width : 0U,
        font != NULL ? font->height : 0U,
        foreground_rgb, background_rgb, pixels, stride_pixels,
        pixel_capacity);
}

int reist_gui_font_raster_xrgb_region(
    const reist_gui_font_t *font, uint32_t glyph_index,
    uint32_t source_x, uint32_t source_y, uint32_t width, uint32_t height,
    uint32_t foreground_rgb, uint32_t background_rgb,
    uint32_t *pixels, uint32_t stride_pixels, size_t pixel_capacity) {
    if (!font_valid(font) || glyph_index >= font->glyph_count ||
        pixels == NULL || width == 0U || height == 0U ||
        source_x >= font->width || source_y >= font->height ||
        width > font->width - source_x || height > font->height - source_y ||
        stride_pixels < width ||
        (foreground_rgb & 0xFF000000U) != 0U ||
        (background_rgb & 0xFF000000U) != 0U ||
        (uint64_t)stride_pixels * height > pixel_capacity) return -22;
    size_t glyph_offset = font->glyph_data_offset +
        (size_t)glyph_index * font->bytes_per_glyph;
    if (glyph_offset > font->data_size ||
        font->bytes_per_glyph > font->data_size - glyph_offset) return -84;
    const uint8_t *glyph = font->data + glyph_offset;
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            uint32_t glyph_x = source_x + x;
            uint32_t glyph_y = source_y + y;
            uint8_t bits = glyph[glyph_y * font->row_bytes + glyph_x / 8U];
            pixels[y * stride_pixels + x] =
                (bits & (uint8_t)(0x80U >> (glyph_x & 7U))) != 0U
                    ? foreground_rgb : background_rgb;
        }
    }
    return 0;
}

int reist_gui_font_scaled_width(const reist_gui_font_t *font,
                                uint32_t target_height,
                                uint32_t *target_width) {
    if (!font_valid(font) || target_width == NULL || target_height == 0U ||
        target_height > REIST_GUI_FONT_MAX_HEIGHT) return -22;
    uint32_t scaled = font->width * target_height +
        font->height / 2U;
    scaled /= font->height;
    if (scaled == 0U) scaled = 1U;
    if (scaled > REIST_GUI_FONT_MAX_WIDTH) return -75;
    *target_width = scaled;
    return 0;
}

int reist_gui_font_raster_scaled_xrgb(
    const reist_gui_font_t *font, uint32_t glyph_index,
    uint32_t target_width, uint32_t target_height,
    uint32_t foreground_rgb, uint32_t background_rgb,
    uint32_t *pixels, uint32_t stride_pixels, size_t pixel_capacity) {
    if (!font_valid(font) || glyph_index >= font->glyph_count ||
        target_width == 0U || target_width > REIST_GUI_FONT_MAX_WIDTH ||
        target_height == 0U || target_height > REIST_GUI_FONT_MAX_HEIGHT ||
        pixels == NULL || stride_pixels < target_width ||
        (foreground_rgb & 0xFF000000U) != 0U ||
        (background_rgb & 0xFF000000U) != 0U ||
        stride_pixels > pixel_capacity / target_height) return -22;
    size_t glyph_offset = font->glyph_data_offset +
        (size_t)glyph_index * font->bytes_per_glyph;
    if (glyph_offset > font->data_size ||
        font->bytes_per_glyph > font->data_size - glyph_offset) return -84;
    const uint8_t *glyph = font->data + glyph_offset;
    for (uint32_t y = 0U; y < target_height; ++y) {
        uint32_t source_y_begin = (y * font->height) / target_height;
        uint32_t source_y_end = source_y_begin + 1U;
        if (target_height < font->height) {
            source_y_end = ((y + 1U) * font->height +
                            target_height - 1U) / target_height;
            if (source_y_end > font->height) source_y_end = font->height;
        }
        for (uint32_t x = 0U; x < target_width; ++x) {
            uint32_t source_x_begin = (x * font->width) / target_width;
            uint32_t source_x_end = source_x_begin + 1U;
            if (target_width < font->width) {
                source_x_end = ((x + 1U) * font->width +
                                target_width - 1U) / target_width;
                if (source_x_end > font->width) source_x_end = font->width;
            }
            uint32_t covered = 0U;
            for (uint32_t source_y = source_y_begin;
                 source_y < source_y_end && !covered; ++source_y) {
                for (uint32_t source_x = source_x_begin;
                     source_x < source_x_end; ++source_x) {
                    uint8_t bits = glyph[
                        source_y * font->row_bytes + source_x / 8U];
                    if ((bits & (uint8_t)(
                            0x80U >> (source_x & 7U))) != 0U) {
                        covered = 1U;
                        break;
                    }
                }
            }
            pixels[y * stride_pixels + x] = covered
                ? foreground_rgb : background_rgb;
        }
    }
    return 0;
}
